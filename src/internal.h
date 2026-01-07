/**
 * LibUThread Internal Header
 *
 * Internal structures and functions not exposed in the public API.
 *
 * @file internal.h
 */

#ifndef UTHREAD_INTERNAL_H
#define UTHREAD_INTERNAL_H

#define _GNU_SOURCE

#include "uthread.h"
#include <ucontext.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/time.h>

/* ==========================================================================
 * Internal Constants
 * ========================================================================== */

/** Maximum cleanup handlers per thread */
#define UTHREAD_CLEANUP_MAX     8

/** Stack guard page size */
#define UTHREAD_GUARD_SIZE      4096

/** CFS target latency in nanoseconds (20ms) */
#define CFS_TARGET_LATENCY_NS   (20 * 1000 * 1000)

/** CFS minimum granularity in nanoseconds (1ms) */
#define CFS_MIN_GRANULARITY_NS  (1 * 1000 * 1000)

/** CFS base weight for nice 0 */
#define CFS_NICE_0_WEIGHT       1024

/* ==========================================================================
 * Wait Queue
 * ========================================================================== */

/** Wait queue for blocking operations */
struct wait_queue {
    struct uthread_internal *head;
    struct uthread_internal *tail;
    int count;
};

/* ==========================================================================
 * Thread Control Block (TCB)
 * ========================================================================== */

/** Internal thread structure */
struct uthread_internal {
    /* Identity */
    int tid;                                /**< Unique thread ID */
    char name[UTHREAD_NAME_MAX];            /**< Thread name */

    /* Execution state */
    ucontext_t context;                     /**< CPU context */
    uthread_state_t state;                  /**< Current state */

    /* Stack */
    void *stack_base;                       /**< Allocated stack base */
    size_t stack_size;                      /**< Stack size */
    void *stack_guard;                      /**< Guard page (if used) */

    /* Entry point */
    void *(*start_routine)(void *);         /**< Thread function */
    void *arg;                              /**< Function argument */
    void *retval;                           /**< Return value */

    /* Scheduling */
    int priority;                           /**< Priority (0-31) */
    int nice;                               /**< Nice value (-20 to +19) */
    int weight;                             /**< CFS weight */
    uint64_t vruntime;                      /**< CFS virtual runtime */
    uint64_t start_time;                    /**< Last schedule start */
    uint64_t total_runtime;                 /**< Total execution time */
    uint64_t timeslice_remaining;           /**< Remaining quantum */

    /* Blocking */
    struct uthread_internal *waiting_on;    /**< Thread we're joining */
    struct wait_queue *blocked_queue;       /**< Queue we're blocked on */

    /* Queue linkage (for run queues and wait queues) */
    struct uthread_internal *next;          /**< Next in queue */
    struct uthread_internal *prev;          /**< Previous in queue */

    /* RB-tree linkage (for CFS) */
    struct uthread_internal *rb_left;       /**< Left child */
    struct uthread_internal *rb_right;      /**< Right child */
    struct uthread_internal *rb_parent;     /**< Parent */
    int rb_color;                           /**< Red or black */

    /* Cleanup handlers */
    void (*cleanup_handlers[UTHREAD_CLEANUP_MAX])(void *);
    void *cleanup_args[UTHREAD_CLEANUP_MAX];
    int cleanup_count;

    /* Flags */
    bool detached;                          /**< Detached thread */
    bool cancel_pending;                    /**< Cancellation requested */
    bool in_critical_section;               /**< In critical section */
    bool exited;                            /**< Thread has exited */

    /* Join synchronization */
    struct uthread_internal *joiner;        /**< Thread joining on us */
};

/* ==========================================================================
 * Scheduler Interface
 * ========================================================================== */

/** Scheduler operations (pluggable interface) */
struct scheduler_ops {
    /** Initialize scheduler */
    int (*init)(void);

    /** Shutdown scheduler */
    void (*shutdown)(void);

    /** Add thread to run queue */
    void (*enqueue)(struct uthread_internal *thread);

    /** Remove and return next thread to run */
    struct uthread_internal *(*dequeue)(void);

    /** Remove specific thread from run queue */
    void (*remove)(struct uthread_internal *thread);

    /** Called when thread yields */
    void (*on_yield)(struct uthread_internal *thread);

    /** Called when timer tick occurs */
    void (*on_tick)(struct uthread_internal *thread, uint64_t elapsed_ns);

    /** Check if preemption needed */
    bool (*should_preempt)(struct uthread_internal *current);

    /** Update thread priority/nice */
    void (*update_priority)(struct uthread_internal *thread);

    /** Get scheduler name */
    const char *(*name)(void);
};

/* ==========================================================================
 * Global Scheduler State
 * ========================================================================== */

/** Global scheduler state */
struct scheduler_state {
    /* Scheduling policy */
    sched_policy_t policy;
    struct scheduler_ops *ops;

    /* Current running thread */
    struct uthread_internal *current;

    /* Idle thread (runs when no other thread ready) */
    struct uthread_internal idle_thread;

    /* All threads */
    struct uthread_internal *all_threads[UTHREAD_MAX_THREADS];
    int thread_count;
    int next_tid;

    /* Timing */
    uint64_t timeslice_ns;
    uint64_t scheduler_ticks;

    /* Statistics */
    uint64_t context_switches;
    uint64_t scheduler_invocations;
    uint64_t total_runtime_ns;
    int total_threads_created;

    /* State flags */
    bool initialized;
    bool preemption_enabled;
    bool in_scheduler;

    /* Signal handling */
    sigset_t block_mask;
    struct sigaction old_sigaction;
};

/** Global scheduler instance */
extern struct scheduler_state g_scheduler;

/* ==========================================================================
 * Round-Robin Scheduler Data
 * ========================================================================== */

/** Round-Robin scheduler state */
struct sched_rr_state {
    struct uthread_internal *head;
    struct uthread_internal *tail;
    int count;
};

extern struct sched_rr_state g_rr_state;
extern struct scheduler_ops sched_rr_ops;

/* ==========================================================================
 * Priority Scheduler Data
 * ========================================================================== */

/** Priority scheduler state */
struct sched_priority_state {
    struct uthread_internal *queues[UTHREAD_PRIORITY_LEVELS];
    uint32_t bitmap;    /* Non-empty queue bitmap */
    int count;
};

extern struct sched_priority_state g_priority_state;
extern struct scheduler_ops sched_priority_ops;

/* ==========================================================================
 * CFS Scheduler Data
 * ========================================================================== */

/** Red-Black tree color */
#define RB_RED      0
#define RB_BLACK    1

/** CFS scheduler state */
struct sched_cfs_state {
    struct uthread_internal *rb_root;
    struct uthread_internal *rb_leftmost;   /* Cache for O(1) access */
    uint64_t min_vruntime;
    int count;
};

extern struct sched_cfs_state g_cfs_state;
extern struct scheduler_ops sched_cfs_ops;

/* ==========================================================================
 * Internal Function Declarations
 * ========================================================================== */

/* Context Management (context.c) */
void context_init(struct uthread_internal *thread);
void context_switch_to(struct uthread_internal *from, struct uthread_internal *to);
void context_entry_wrapper(void);

/* Scheduler Core (scheduler.c) */
void scheduler_init_common(void);
void scheduler_schedule(void);
void scheduler_yield(void);
void scheduler_block(struct wait_queue *wq);
void scheduler_unblock(struct uthread_internal *thread);
void scheduler_tick(void);
struct uthread_internal *scheduler_current(void);
void scheduler_add_thread(struct uthread_internal *thread);
void scheduler_remove_thread(struct uthread_internal *thread);

/* Timer/Preemption (timer.c) */
int timer_init(void);
void timer_shutdown(void);
void timer_start(void);
void timer_stop(void);
void timer_set_interval(uint64_t ns);
void preemption_disable(void);
void preemption_enable(void);
bool preemption_is_enabled(void);

/* Wait Queue Operations */
void wait_queue_init(struct wait_queue *wq);
void wait_queue_destroy(struct wait_queue *wq);
void wait_queue_add(struct wait_queue *wq, struct uthread_internal *thread);
struct uthread_internal *wait_queue_remove(struct wait_queue *wq);
struct uthread_internal *wait_queue_remove_specific(struct wait_queue *wq,
                                                     struct uthread_internal *thread);
bool wait_queue_empty(struct wait_queue *wq);
void wait_queue_wake_one(struct wait_queue *wq);
void wait_queue_wake_all(struct wait_queue *wq);

/* Thread Internal Operations (uthread.c) */
struct uthread_internal *thread_alloc(void);
void thread_free(struct uthread_internal *thread);
int thread_setup_stack(struct uthread_internal *thread, size_t size);
void thread_cleanup(struct uthread_internal *thread);

/* Utility Functions */
uint64_t get_time_ns(void);
int nice_to_weight(int nice);

/* CFS RB-Tree Operations (sched_cfs.c) */
void rb_insert(struct uthread_internal *thread);
void rb_remove(struct uthread_internal *thread);
struct uthread_internal *rb_leftmost(void);

/* Debug */
#ifdef DEBUG
#define UTHREAD_DEBUG(fmt, ...) \
    fprintf(stderr, "[uthread] " fmt "\n", ##__VA_ARGS__)
#else
#define UTHREAD_DEBUG(fmt, ...) ((void)0)
#endif

#define UTHREAD_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[uthread] Assertion failed: %s at %s:%d\n", \
                    #cond, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

#endif /* UTHREAD_INTERNAL_H */
