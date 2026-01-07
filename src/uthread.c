/**
 * LibUThread Core Implementation
 *
 * Main thread management functions: create, join, exit, yield, etc.
 *
 * @file uthread.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

/* Global scheduler state */
struct scheduler_state g_scheduler = {0};

/* ==========================================================================
 * Library Initialization
 * ========================================================================== */

int uthread_init(sched_policy_t policy)
{
    if (g_scheduler.initialized) {
        return UTHREAD_EINVAL;
    }

    /* Initialize scheduler state */
    memset(&g_scheduler, 0, sizeof(g_scheduler));
    g_scheduler.policy = policy;
    g_scheduler.timeslice_ns = UTHREAD_TIMESLICE_DEFAULT_NS;
    g_scheduler.preemption_enabled = true;
    g_scheduler.next_tid = 1;

    /* Select scheduler implementation */
    switch (policy) {
    case SCHED_ROUND_ROBIN:
        g_scheduler.ops = &sched_rr_ops;
        break;
    case SCHED_PRIORITY:
        g_scheduler.ops = &sched_priority_ops;
        break;
    case SCHED_CFS:
        g_scheduler.ops = &sched_cfs_ops;
        break;
    default:
        return UTHREAD_EINVAL;
    }

    /* Initialize the scheduler */
    if (g_scheduler.ops->init() != 0) {
        return UTHREAD_ENOMEM;
    }

    /* Set up the idle thread */
    memset(&g_scheduler.idle_thread, 0, sizeof(g_scheduler.idle_thread));
    g_scheduler.idle_thread.tid = 0;
    g_scheduler.idle_thread.state = UTHREAD_STATE_READY;
    strncpy(g_scheduler.idle_thread.name, "idle", UTHREAD_NAME_MAX - 1);

    /* Initialize main thread as the first user thread */
    struct uthread_internal *main_thread = thread_alloc();
    if (main_thread == NULL) {
        g_scheduler.ops->shutdown();
        return UTHREAD_ENOMEM;
    }

    main_thread->tid = g_scheduler.next_tid++;
    main_thread->state = UTHREAD_STATE_RUNNING;
    main_thread->priority = UTHREAD_PRIORITY_DEFAULT;
    main_thread->nice = 0;
    main_thread->weight = CFS_NICE_0_WEIGHT;
    main_thread->detached = false;
    strncpy(main_thread->name, "main", UTHREAD_NAME_MAX - 1);

    /* Get the current context for main thread */
    if (getcontext(&main_thread->context) == -1) {
        thread_free(main_thread);
        g_scheduler.ops->shutdown();
        return UTHREAD_ENOMEM;
    }

    /* Main thread uses the process stack, so no separate allocation */
    main_thread->stack_base = NULL;
    main_thread->stack_size = 0;

    /* Set as current thread */
    g_scheduler.current = main_thread;
    scheduler_add_thread(main_thread);

    /* Initialize the timer for preemption */
    if (timer_init() != 0) {
        thread_free(main_thread);
        g_scheduler.ops->shutdown();
        return UTHREAD_ENOMEM;
    }

    g_scheduler.initialized = true;

    /* Start the preemption timer */
    timer_start();

    UTHREAD_DEBUG("Library initialized with %s scheduler",
                  g_scheduler.ops->name());

    return UTHREAD_SUCCESS;
}

void uthread_shutdown(void)
{
    if (!g_scheduler.initialized) {
        return;
    }

    /* Stop preemption */
    timer_stop();
    timer_shutdown();

    /* Clean up all threads */
    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        struct uthread_internal *t = g_scheduler.all_threads[i];
        if (t != NULL) {
            thread_free(t);
            g_scheduler.all_threads[i] = NULL;
        }
    }

    /* Shutdown scheduler */
    if (g_scheduler.ops != NULL) {
        g_scheduler.ops->shutdown();
    }

    g_scheduler.initialized = false;

    UTHREAD_DEBUG("Library shutdown complete");
}

bool uthread_is_initialized(void)
{
    return g_scheduler.initialized;
}

sched_policy_t uthread_get_policy(void)
{
    return g_scheduler.policy;
}

/* ==========================================================================
 * Thread Creation and Management
 * ========================================================================== */

int uthread_create(uthread_t *thread,
                   const uthread_attr_t *attr,
                   void *(*start)(void *),
                   void *arg)
{
    if (!g_scheduler.initialized) {
        return UTHREAD_EINVAL;
    }

    if (thread == NULL || start == NULL) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    /* Allocate thread structure */
    struct uthread_internal *t = thread_alloc();
    if (t == NULL) {
        preemption_enable();
        return UTHREAD_ENOMEM;
    }

    /* Set thread ID */
    t->tid = g_scheduler.next_tid++;

    /* Set up from attributes or defaults */
    size_t stack_size = UTHREAD_STACK_DEFAULT;
    if (attr != NULL) {
        if (attr->stack_size >= UTHREAD_STACK_MIN) {
            stack_size = attr->stack_size;
        }
        t->priority = attr->priority;
        t->nice = attr->nice;
        t->detached = (attr->detach_state == UTHREAD_CREATE_DETACHED);
        if (attr->name[0] != '\0') {
            strncpy(t->name, attr->name, UTHREAD_NAME_MAX - 1);
        }
    } else {
        t->priority = UTHREAD_PRIORITY_DEFAULT;
        t->nice = 0;
        t->detached = false;
    }

    /* Calculate CFS weight */
    t->weight = nice_to_weight(t->nice);

    /* Allocate stack */
    if (thread_setup_stack(t, stack_size) != 0) {
        thread_free(t);
        preemption_enable();
        return UTHREAD_ENOMEM;
    }

    /* Set entry point */
    t->start_routine = start;
    t->arg = arg;
    t->state = UTHREAD_STATE_READY;

    /* Initialize context */
    context_init(t);

    /* Add to scheduler */
    scheduler_add_thread(t);
    g_scheduler.ops->enqueue(t);

    g_scheduler.total_threads_created++;

    *thread = t;

    UTHREAD_DEBUG("Created thread %d '%s' (stack=%zu, priority=%d)",
                  t->tid, t->name, t->stack_size, t->priority);

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_join(uthread_t thread, void **retval)
{
    if (!g_scheduler.initialized || thread == NULL) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;
    struct uthread_internal *self = g_scheduler.current;

    /* Cannot join self */
    if (t == self) {
        return UTHREAD_EDEADLK;
    }

    /* Cannot join detached thread */
    if (t->detached) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    /* Check if already joined by another thread */
    if (t->joiner != NULL && t->joiner != self) {
        preemption_enable();
        return UTHREAD_EINVAL;
    }

    /* If thread hasn't exited yet, block */
    while (!t->exited) {
        t->joiner = self;
        self->waiting_on = t;
        self->state = UTHREAD_STATE_BLOCKED;

        /* Schedule another thread */
        scheduler_schedule();

        /* When we wake up, check again */
    }

    /* Thread has exited, get return value */
    if (retval != NULL) {
        *retval = t->retval;
    }

    /* Clean up the thread */
    scheduler_remove_thread(t);
    thread_free(t);

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_detach(uthread_t thread)
{
    if (!g_scheduler.initialized || thread == NULL) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;

    preemption_disable();

    if (t->detached) {
        preemption_enable();
        return UTHREAD_EINVAL;
    }

    if (t->joiner != NULL) {
        preemption_enable();
        return UTHREAD_EINVAL;
    }

    t->detached = true;

    /* If thread already exited, clean up now */
    if (t->exited) {
        scheduler_remove_thread(t);
        thread_free(t);
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}

void uthread_yield(void)
{
    if (!g_scheduler.initialized) {
        return;
    }

    preemption_disable();

    struct uthread_internal *self = g_scheduler.current;
    if (self != NULL) {
        g_scheduler.ops->on_yield(self);
        scheduler_yield();
    }

    preemption_enable();
}

void uthread_exit(void *retval)
{
    if (!g_scheduler.initialized) {
        exit(0);
    }

    preemption_disable();

    struct uthread_internal *self = g_scheduler.current;
    if (self == NULL) {
        preemption_enable();
        exit(0);
    }

    UTHREAD_DEBUG("Thread %d '%s' exiting", self->tid, self->name);

    /* Run cleanup handlers in reverse order */
    while (self->cleanup_count > 0) {
        self->cleanup_count--;
        if (self->cleanup_handlers[self->cleanup_count] != NULL) {
            self->cleanup_handlers[self->cleanup_count](
                self->cleanup_args[self->cleanup_count]);
        }
    }

    /* Store return value */
    self->retval = retval;
    self->exited = true;
    self->state = UTHREAD_STATE_TERMINATED;

    /* Remove from run queue */
    g_scheduler.ops->remove(self);

    /* Wake up joiner if any */
    if (self->joiner != NULL) {
        self->joiner->waiting_on = NULL;
        self->joiner->state = UTHREAD_STATE_READY;
        g_scheduler.ops->enqueue(self->joiner);
    }

    /* If detached, schedule cleanup */
    if (self->detached) {
        scheduler_remove_thread(self);
        thread_free(self);
    }

    /* Schedule next thread (never returns) */
    scheduler_schedule();

    /* Should never reach here */
    UTHREAD_ASSERT(0 && "uthread_exit: scheduler returned");
    __builtin_unreachable();
}

uthread_t uthread_self(void)
{
    if (!g_scheduler.initialized) {
        return NULL;
    }
    return (uthread_t)g_scheduler.current;
}

int uthread_equal(uthread_t t1, uthread_t t2)
{
    return (t1 == t2) ? 1 : 0;
}

void uthread_sleep(unsigned int milliseconds)
{
    if (!g_scheduler.initialized || milliseconds == 0) {
        return;
    }

    /*
     * Simple implementation: busy-yield until time elapsed.
     * A better implementation would use a sleep queue.
     */
    uint64_t start = get_time_ns();
    uint64_t target = start + (uint64_t)milliseconds * 1000000ULL;

    while (get_time_ns() < target) {
        uthread_yield();
    }
}

int uthread_get_tid(uthread_t thread)
{
    if (thread == NULL) {
        return -1;
    }
    struct uthread_internal *t = (struct uthread_internal *)thread;
    return t->tid;
}

int uthread_setname(uthread_t thread, const char *name)
{
    if (thread == NULL || name == NULL) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;
    strncpy(t->name, name, UTHREAD_NAME_MAX - 1);
    t->name[UTHREAD_NAME_MAX - 1] = '\0';

    return UTHREAD_SUCCESS;
}

int uthread_getname(uthread_t thread, char *name, size_t len)
{
    if (thread == NULL || name == NULL || len == 0) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;
    strncpy(name, t->name, len - 1);
    name[len - 1] = '\0';

    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Thread Attributes
 * ========================================================================== */

int uthread_attr_init(uthread_attr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    memset(attr, 0, sizeof(*attr));
    attr->stack_size = UTHREAD_STACK_DEFAULT;
    attr->priority = UTHREAD_PRIORITY_DEFAULT;
    attr->nice = 0;
    attr->detach_state = UTHREAD_CREATE_JOINABLE;

    return UTHREAD_SUCCESS;
}

int uthread_attr_destroy(uthread_attr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }
    /* Nothing to free */
    return UTHREAD_SUCCESS;
}

int uthread_attr_setstacksize(uthread_attr_t *attr, size_t size)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (size < UTHREAD_STACK_MIN || size > UTHREAD_STACK_MAX) {
        return UTHREAD_EINVAL;
    }

    attr->stack_size = size;
    return UTHREAD_SUCCESS;
}

int uthread_attr_getstacksize(const uthread_attr_t *attr, size_t *size)
{
    if (attr == NULL || size == NULL) {
        return UTHREAD_EINVAL;
    }

    *size = attr->stack_size;
    return UTHREAD_SUCCESS;
}

int uthread_attr_setpriority(uthread_attr_t *attr, int priority)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (priority < UTHREAD_PRIORITY_MIN || priority > UTHREAD_PRIORITY_MAX) {
        return UTHREAD_EINVAL;
    }

    attr->priority = priority;
    return UTHREAD_SUCCESS;
}

int uthread_attr_getpriority(const uthread_attr_t *attr, int *priority)
{
    if (attr == NULL || priority == NULL) {
        return UTHREAD_EINVAL;
    }

    *priority = attr->priority;
    return UTHREAD_SUCCESS;
}

int uthread_attr_setnice(uthread_attr_t *attr, int nice)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (nice < -20 || nice > 19) {
        return UTHREAD_EINVAL;
    }

    attr->nice = nice;
    return UTHREAD_SUCCESS;
}

int uthread_attr_getnice(const uthread_attr_t *attr, int *nice)
{
    if (attr == NULL || nice == NULL) {
        return UTHREAD_EINVAL;
    }

    *nice = attr->nice;
    return UTHREAD_SUCCESS;
}

int uthread_attr_setdetachstate(uthread_attr_t *attr, int detachstate)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (detachstate != UTHREAD_CREATE_JOINABLE &&
        detachstate != UTHREAD_CREATE_DETACHED) {
        return UTHREAD_EINVAL;
    }

    attr->detach_state = detachstate;
    return UTHREAD_SUCCESS;
}

int uthread_attr_getdetachstate(const uthread_attr_t *attr, int *detachstate)
{
    if (attr == NULL || detachstate == NULL) {
        return UTHREAD_EINVAL;
    }

    *detachstate = attr->detach_state;
    return UTHREAD_SUCCESS;
}

int uthread_attr_setname(uthread_attr_t *attr, const char *name)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (name != NULL) {
        strncpy(attr->name, name, UTHREAD_NAME_MAX - 1);
        attr->name[UTHREAD_NAME_MAX - 1] = '\0';
    } else {
        attr->name[0] = '\0';
    }

    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Internal Thread Operations
 * ========================================================================== */

struct uthread_internal *thread_alloc(void)
{
    struct uthread_internal *t = calloc(1, sizeof(struct uthread_internal));
    if (t == NULL) {
        return NULL;
    }

    /* Initialize to safe defaults */
    t->state = UTHREAD_STATE_READY;
    t->priority = UTHREAD_PRIORITY_DEFAULT;
    t->weight = CFS_NICE_0_WEIGHT;

    return t;
}

void thread_free(struct uthread_internal *thread)
{
    if (thread == NULL) {
        return;
    }

    /* Free the stack if we allocated one */
    if (thread->stack_base != NULL) {
        /* If we used mmap with guard page, unmap the whole region */
        if (thread->stack_guard != NULL) {
            munmap(thread->stack_guard, thread->stack_size + UTHREAD_GUARD_SIZE);
        } else {
            free(thread->stack_base);
        }
    }

    free(thread);
}

int thread_setup_stack(struct uthread_internal *thread, size_t size)
{
    /*
     * Allocate stack with guard page for overflow detection.
     *
     * Memory layout:
     * [guard page (PROT_NONE)] [usable stack]
     *
     * Stack grows down, so guard page is at the low address.
     */
    size_t total_size = size + UTHREAD_GUARD_SIZE;

    void *region = mmap(NULL, total_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);

    if (region == MAP_FAILED) {
        /* Fall back to simple allocation without guard */
        thread->stack_base = aligned_alloc(16, size);
        if (thread->stack_base == NULL) {
            return UTHREAD_ENOMEM;
        }
        thread->stack_size = size;
        thread->stack_guard = NULL;
        return UTHREAD_SUCCESS;
    }

    /* Set up guard page (no access) */
    if (mprotect(region, UTHREAD_GUARD_SIZE, PROT_NONE) == -1) {
        munmap(region, total_size);
        return UTHREAD_ENOMEM;
    }

    thread->stack_guard = region;
    thread->stack_base = (char *)region + UTHREAD_GUARD_SIZE;
    thread->stack_size = size;

    return UTHREAD_SUCCESS;
}

void thread_cleanup(struct uthread_internal *thread)
{
    if (thread == NULL) {
        return;
    }

    /* Run cleanup handlers */
    while (thread->cleanup_count > 0) {
        thread->cleanup_count--;
        if (thread->cleanup_handlers[thread->cleanup_count] != NULL) {
            thread->cleanup_handlers[thread->cleanup_count](
                thread->cleanup_args[thread->cleanup_count]);
        }
    }
}

/* ==========================================================================
 * Scheduler Control
 * ========================================================================== */

int uthread_set_timeslice(uint64_t ns)
{
    if (!g_scheduler.initialized) {
        return UTHREAD_EINVAL;
    }

    if (ns < 1000000) {  /* Minimum 1ms */
        return UTHREAD_EINVAL;
    }

    preemption_disable();
    g_scheduler.timeslice_ns = ns;
    timer_set_interval(ns);
    preemption_enable();

    return UTHREAD_SUCCESS;
}

uint64_t uthread_get_timeslice(void)
{
    return g_scheduler.timeslice_ns;
}

bool uthread_set_preemption(bool enable)
{
    bool old = g_scheduler.preemption_enabled;
    g_scheduler.preemption_enabled = enable;

    if (enable && g_scheduler.initialized) {
        timer_start();
    } else if (!enable && g_scheduler.initialized) {
        timer_stop();
    }

    return old;
}

int uthread_setpriority(uthread_t thread, int priority)
{
    if (!g_scheduler.initialized || thread == NULL) {
        return UTHREAD_EINVAL;
    }

    if (priority < UTHREAD_PRIORITY_MIN || priority > UTHREAD_PRIORITY_MAX) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;

    preemption_disable();
    t->priority = priority;
    g_scheduler.ops->update_priority(t);
    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_getpriority(uthread_t thread, int *priority)
{
    if (thread == NULL || priority == NULL) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;
    *priority = t->priority;

    return UTHREAD_SUCCESS;
}

int uthread_setnice(uthread_t thread, int nice)
{
    if (!g_scheduler.initialized || thread == NULL) {
        return UTHREAD_EINVAL;
    }

    if (nice < -20 || nice > 19) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;

    preemption_disable();
    t->nice = nice;
    t->weight = nice_to_weight(nice);
    g_scheduler.ops->update_priority(t);
    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_getnice(uthread_t thread, int *nice)
{
    if (thread == NULL || nice == NULL) {
        return UTHREAD_EINVAL;
    }

    struct uthread_internal *t = (struct uthread_internal *)thread;
    *nice = t->nice;

    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

int uthread_get_stats(uthread_stats_t *stats)
{
    if (stats == NULL) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    stats->total_threads = g_scheduler.total_threads_created;
    stats->active_threads = g_scheduler.thread_count;
    stats->context_switches = g_scheduler.context_switches;
    stats->scheduler_invocations = g_scheduler.scheduler_invocations;
    stats->total_runtime_ns = g_scheduler.total_runtime_ns;

    /* Count ready and blocked threads */
    stats->ready_threads = 0;
    stats->blocked_threads = 0;
    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        struct uthread_internal *t = g_scheduler.all_threads[i];
        if (t != NULL) {
            if (t->state == UTHREAD_STATE_READY) {
                stats->ready_threads++;
            } else if (t->state == UTHREAD_STATE_BLOCKED) {
                stats->blocked_threads++;
            }
        }
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}

void uthread_reset_stats(void)
{
    preemption_disable();
    g_scheduler.context_switches = 0;
    g_scheduler.scheduler_invocations = 0;
    g_scheduler.total_runtime_ns = 0;
    preemption_enable();
}

void uthread_debug_dump(void)
{
    preemption_disable();

    fprintf(stderr, "\n=== UThread Debug Dump ===\n");
    fprintf(stderr, "Scheduler: %s\n", g_scheduler.ops->name());
    fprintf(stderr, "Timeslice: %lu ns\n", (unsigned long)g_scheduler.timeslice_ns);
    fprintf(stderr, "Total threads created: %d\n", g_scheduler.total_threads_created);
    fprintf(stderr, "Active threads: %d\n", g_scheduler.thread_count);
    fprintf(stderr, "Context switches: %lu\n", (unsigned long)g_scheduler.context_switches);
    fprintf(stderr, "\nThread list:\n");

    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        struct uthread_internal *t = g_scheduler.all_threads[i];
        if (t != NULL) {
            const char *state_str;
            switch (t->state) {
            case UTHREAD_STATE_READY: state_str = "READY"; break;
            case UTHREAD_STATE_RUNNING: state_str = "RUNNING"; break;
            case UTHREAD_STATE_BLOCKED: state_str = "BLOCKED"; break;
            case UTHREAD_STATE_TERMINATED: state_str = "TERMINATED"; break;
            default: state_str = "UNKNOWN"; break;
            }

            fprintf(stderr, "  [%d] '%s' state=%s priority=%d nice=%d\n",
                    t->tid, t->name, state_str, t->priority, t->nice);
        }
    }

    fprintf(stderr, "==========================\n\n");

    preemption_enable();
}
