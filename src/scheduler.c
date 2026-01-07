/**
 * LibUThread Scheduler Core
 *
 * Common scheduler infrastructure and wait queue operations.
 *
 * @file scheduler.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * Wait Queue Operations
 * ========================================================================== */

void wait_queue_init(struct wait_queue *wq)
{
    if (wq == NULL) return;

    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
}

void wait_queue_destroy(struct wait_queue *wq)
{
    if (wq == NULL) return;

    /* Just reset - actual thread cleanup is done elsewhere */
    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
}

void wait_queue_add(struct wait_queue *wq, struct uthread_internal *thread)
{
    if (wq == NULL || thread == NULL) return;

    thread->next = NULL;
    thread->prev = wq->tail;

    if (wq->tail != NULL) {
        wq->tail->next = thread;
    } else {
        wq->head = thread;
    }

    wq->tail = thread;
    wq->count++;

    thread->blocked_queue = wq;
}

struct uthread_internal *wait_queue_remove(struct wait_queue *wq)
{
    if (wq == NULL || wq->head == NULL) return NULL;

    struct uthread_internal *thread = wq->head;

    wq->head = thread->next;
    if (wq->head != NULL) {
        wq->head->prev = NULL;
    } else {
        wq->tail = NULL;
    }

    thread->next = NULL;
    thread->prev = NULL;
    thread->blocked_queue = NULL;
    wq->count--;

    return thread;
}

struct uthread_internal *wait_queue_remove_specific(struct wait_queue *wq,
                                                     struct uthread_internal *thread)
{
    if (wq == NULL || thread == NULL) return NULL;
    if (thread->blocked_queue != wq) return NULL;

    /* Update previous node */
    if (thread->prev != NULL) {
        thread->prev->next = thread->next;
    } else {
        wq->head = thread->next;
    }

    /* Update next node */
    if (thread->next != NULL) {
        thread->next->prev = thread->prev;
    } else {
        wq->tail = thread->prev;
    }

    thread->next = NULL;
    thread->prev = NULL;
    thread->blocked_queue = NULL;
    wq->count--;

    return thread;
}

bool wait_queue_empty(struct wait_queue *wq)
{
    return (wq == NULL || wq->head == NULL);
}

void wait_queue_wake_one(struct wait_queue *wq)
{
    if (wq == NULL) return;

    struct uthread_internal *thread = wait_queue_remove(wq);
    if (thread != NULL) {
        scheduler_unblock(thread);
    }
}

void wait_queue_wake_all(struct wait_queue *wq)
{
    if (wq == NULL) return;

    struct uthread_internal *thread;
    while ((thread = wait_queue_remove(wq)) != NULL) {
        scheduler_unblock(thread);
    }
}

/* ==========================================================================
 * Scheduler Core Operations
 * ========================================================================== */

void scheduler_init_common(void)
{
    /* Common initialization for all schedulers */
}

struct uthread_internal *scheduler_current(void)
{
    return g_scheduler.current;
}

void scheduler_add_thread(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Find a free slot */
    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        if (g_scheduler.all_threads[i] == NULL) {
            g_scheduler.all_threads[i] = thread;
            g_scheduler.thread_count++;
            return;
        }
    }

    UTHREAD_ASSERT(0 && "scheduler_add_thread: no free slots");
}

void scheduler_remove_thread(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        if (g_scheduler.all_threads[i] == thread) {
            g_scheduler.all_threads[i] = NULL;
            g_scheduler.thread_count--;
            return;
        }
    }
}

void scheduler_schedule(void)
{
    g_scheduler.scheduler_invocations++;
    g_scheduler.in_scheduler = true;

    struct uthread_internal *current = g_scheduler.current;
    struct uthread_internal *next;

    /* Get next thread from scheduler */
    next = g_scheduler.ops->dequeue();

    /* If no thread ready, use idle thread */
    if (next == NULL) {
        next = &g_scheduler.idle_thread;
    }

    /* If same thread, just return */
    if (next == current) {
        g_scheduler.in_scheduler = false;
        return;
    }

    /* Update states */
    if (current != NULL && current->state == UTHREAD_STATE_RUNNING) {
        current->state = UTHREAD_STATE_READY;
    }

    next->state = UTHREAD_STATE_RUNNING;
    g_scheduler.current = next;

    UTHREAD_DEBUG("Switch: %d '%s' -> %d '%s'",
                  current ? current->tid : -1,
                  current ? current->name : "none",
                  next->tid, next->name);

    g_scheduler.in_scheduler = false;

    /* Perform context switch */
    if (current != NULL) {
        context_switch_to(current, next);
    } else {
        /* First switch - just restore next */
        setcontext(&next->context);
    }
}

void scheduler_yield(void)
{
    struct uthread_internal *current = g_scheduler.current;

    if (current == NULL || current == &g_scheduler.idle_thread) {
        return;
    }

    /* Put current thread back in run queue */
    if (current->state == UTHREAD_STATE_RUNNING) {
        current->state = UTHREAD_STATE_READY;
        g_scheduler.ops->enqueue(current);
    }

    scheduler_schedule();
}

void scheduler_block(struct wait_queue *wq)
{
    struct uthread_internal *current = g_scheduler.current;

    if (current == NULL) {
        return;
    }

    /* Add to wait queue */
    current->state = UTHREAD_STATE_BLOCKED;
    wait_queue_add(wq, current);

    /* Schedule another thread */
    scheduler_schedule();
}

void scheduler_unblock(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    thread->state = UTHREAD_STATE_READY;
    g_scheduler.ops->enqueue(thread);
}

void scheduler_tick(void)
{
    g_scheduler.scheduler_ticks++;

    struct uthread_internal *current = g_scheduler.current;
    if (current == NULL || current == &g_scheduler.idle_thread) {
        return;
    }

    /* Calculate elapsed time */
    uint64_t now = get_time_ns();
    uint64_t elapsed = now - current->start_time;

    /* Notify scheduler */
    g_scheduler.ops->on_tick(current, elapsed);

    /* Check if preemption needed */
    if (g_scheduler.preemption_enabled &&
        g_scheduler.ops->should_preempt(current)) {

        UTHREAD_DEBUG("Preempting thread %d '%s'", current->tid, current->name);

        /* Put current back in queue and reschedule */
        current->state = UTHREAD_STATE_READY;
        g_scheduler.ops->enqueue(current);
        scheduler_schedule();
    }
}

/* ==========================================================================
 * Idle Thread
 * ========================================================================== */

static void idle_thread_func(void)
{
    /*
     * Idle thread just yields in a loop.
     * In a real implementation, this could:
     * - Put the CPU in a low-power state
     * - Poll for I/O completions
     * - Run background tasks
     */
    while (1) {
        /* Yield to any ready thread */
        scheduler_yield();
    }
}

/* Note: Idle thread initialization is handled in uthread_init() */
