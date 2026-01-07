/**
 * LibUThread Context Management
 *
 * Implements context switching using ucontext API.
 *
 * @file context.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Thread entry wrapper function.
 *
 * This function is the actual entry point for all threads.
 * It calls the user's start routine and handles thread exit.
 */
void context_entry_wrapper(void)
{
    struct uthread_internal *self = scheduler_current();

    UTHREAD_ASSERT(self != NULL);
    UTHREAD_ASSERT(self->start_routine != NULL);

    /* Enable preemption now that we're running */
    preemption_enable();

    /* Call the user's thread function */
    void *retval = self->start_routine(self->arg);

    /* Thread returned normally - call exit */
    uthread_exit(retval);

    /* Should never reach here */
    UTHREAD_ASSERT(0 && "context_entry_wrapper: uthread_exit returned");
}

/**
 * Initialize a thread's context.
 *
 * Sets up the ucontext structure so the thread will start
 * execution at context_entry_wrapper.
 *
 * @param thread Thread to initialize
 */
void context_init(struct uthread_internal *thread)
{
    UTHREAD_ASSERT(thread != NULL);
    UTHREAD_ASSERT(thread->stack_base != NULL);
    UTHREAD_ASSERT(thread->stack_size >= UTHREAD_STACK_MIN);

    /* Get current context as a base */
    if (getcontext(&thread->context) == -1) {
        perror("getcontext");
        abort();
    }

    /* Set up the stack */
    thread->context.uc_stack.ss_sp = thread->stack_base;
    thread->context.uc_stack.ss_size = thread->stack_size;
    thread->context.uc_stack.ss_flags = 0;

    /* No successor context - thread will call uthread_exit */
    thread->context.uc_link = NULL;

    /* Create the context to start at our wrapper function */
    makecontext(&thread->context, context_entry_wrapper, 0);
}

/**
 * Perform a context switch from one thread to another.
 *
 * Saves the current context and restores the target context.
 * This function returns when the 'from' thread is scheduled again.
 *
 * @param from Current thread (context will be saved here)
 * @param to   Target thread (context will be restored from here)
 */
void context_switch_to(struct uthread_internal *from, struct uthread_internal *to)
{
    UTHREAD_ASSERT(from != NULL);
    UTHREAD_ASSERT(to != NULL);

    /* Record timing */
    uint64_t now = get_time_ns();
    if (from->start_time > 0) {
        uint64_t elapsed = now - from->start_time;
        from->total_runtime += elapsed;
    }
    to->start_time = now;

    /* Update statistics */
    g_scheduler.context_switches++;

    /* Swap contexts - saves 'from' and restores 'to' */
    if (swapcontext(&from->context, &to->context) == -1) {
        perror("swapcontext");
        abort();
    }

    /* When we return here, 'from' has been scheduled again */
}

/**
 * Get current time in nanoseconds.
 *
 * Uses CLOCK_MONOTONIC for consistent timing.
 *
 * @return Current time in nanoseconds
 */
uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Convert nice value to CFS weight.
 *
 * Nice values range from -20 (highest priority) to +19 (lowest).
 * Weight is used in CFS to calculate virtual runtime.
 *
 * @param nice Nice value (-20 to +19)
 * @return CFS weight
 */
int nice_to_weight(int nice)
{
    /*
     * Weight table from Linux kernel (simplified).
     * Each nice level changes weight by ~1.25x (25%).
     */
    static const int weight_table[40] = {
        /* -20 */ 88761, 71755, 56483, 46273, 36291,
        /* -15 */ 29154, 23254, 18705, 14949, 11916,
        /* -10 */  9548,  7620,  6100,  4904,  3906,
        /*  -5 */  3121,  2501,  1991,  1586,  1277,
        /*   0 */  1024,   820,   655,   526,   423,
        /*   5 */   335,   272,   215,   172,   137,
        /*  10 */   110,    87,    70,    56,    45,
        /*  15 */    36,    29,    23,    18,    15,
    };

    /* Clamp nice to valid range */
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;

    return weight_table[nice + 20];
}
