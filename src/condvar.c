/**
 * LibUThread Condition Variable Implementation
 *
 * Condition variables for thread synchronization.
 *
 * @file condvar.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==========================================================================
 * Condition Variable Attribute Functions
 * ========================================================================== */

int uthread_condattr_init(uthread_condattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    attr->clock_id = CLOCK_MONOTONIC;
    return UTHREAD_SUCCESS;
}

int uthread_condattr_destroy(uthread_condattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Nothing to free */
    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Condition Variable Functions
 * ========================================================================== */

int uthread_cond_init(uthread_cond_t *cond, const uthread_condattr_t *attr)
{
    if (cond == NULL) {
        return UTHREAD_EINVAL;
    }

    (void)attr;  /* Currently unused */

    memset(cond, 0, sizeof(*cond));
    cond->signal_seq = 0;

    /* Allocate wait queue */
    cond->waiters = malloc(sizeof(struct wait_queue));
    if (cond->waiters == NULL) {
        return UTHREAD_ENOMEM;
    }
    wait_queue_init(cond->waiters);

    cond->initialized = true;

    return UTHREAD_SUCCESS;
}

int uthread_cond_destroy(uthread_cond_t *cond)
{
    if (cond == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!cond->initialized) {
        return UTHREAD_EINVAL;
    }

    /* Cannot destroy if threads are waiting */
    if (cond->waiters != NULL && !wait_queue_empty(cond->waiters)) {
        return UTHREAD_EBUSY;
    }

    /* Free wait queue */
    if (cond->waiters != NULL) {
        wait_queue_destroy(cond->waiters);
        free(cond->waiters);
        cond->waiters = NULL;
    }

    cond->initialized = false;

    return UTHREAD_SUCCESS;
}

int uthread_cond_wait(uthread_cond_t *cond, uthread_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized condvar (static initializer) */
    if (!cond->initialized) {
        cond->waiters = malloc(sizeof(struct wait_queue));
        if (cond->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(cond->waiters);
        cond->initialized = true;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();
    if (self == NULL) {
        preemption_enable();
        return UTHREAD_EINVAL;
    }

    /* Remember the sequence number to detect signals */
    uint64_t seq = cond->signal_seq;

    /* Add ourselves to the wait queue */
    self->state = UTHREAD_STATE_BLOCKED;
    wait_queue_add(cond->waiters, self);

    /* Release the mutex atomically with blocking */
    mutex->lock = 0;
    mutex->owner = NULL;

    /* Wake one waiter on the mutex if any */
    if (mutex->waiters != NULL && !wait_queue_empty(mutex->waiters)) {
        wait_queue_wake_one(mutex->waiters);
    }

    /* Enable preemption and schedule another thread */
    preemption_enable();
    scheduler_schedule();

    /*
     * When we wake up, we need to reacquire the mutex.
     * We might have been woken spuriously, so we don't check seq.
     */
    preemption_disable();

    /* Try to reacquire the mutex */
    while (mutex->lock != 0) {
        /* Block on mutex */
        self->state = UTHREAD_STATE_BLOCKED;
        wait_queue_add(mutex->waiters, self);

        preemption_enable();
        scheduler_schedule();
        preemption_disable();
    }

    /* Acquired the mutex */
    mutex->lock = 1;
    mutex->owner = self;
    mutex->recursion_count = 1;

    preemption_enable();

    (void)seq;  /* Unused - we allow spurious wakeups */
    return UTHREAD_SUCCESS;
}

int uthread_cond_timedwait(uthread_cond_t *cond,
                           uthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    if (cond == NULL || mutex == NULL || abstime == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized condvar */
    if (!cond->initialized) {
        cond->waiters = malloc(sizeof(struct wait_queue));
        if (cond->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(cond->waiters);
        cond->initialized = true;
    }

    /* Convert abstime to deadline in nanoseconds */
    uint64_t deadline = (uint64_t)abstime->tv_sec * 1000000000ULL +
                        (uint64_t)abstime->tv_nsec;

    preemption_disable();

    struct uthread_internal *self = scheduler_current();
    if (self == NULL) {
        preemption_enable();
        return UTHREAD_EINVAL;
    }

    uint64_t seq = cond->signal_seq;

    /* Add ourselves to the wait queue */
    self->state = UTHREAD_STATE_BLOCKED;
    wait_queue_add(cond->waiters, self);

    /* Release the mutex */
    mutex->lock = 0;
    mutex->owner = NULL;

    if (mutex->waiters != NULL && !wait_queue_empty(mutex->waiters)) {
        wait_queue_wake_one(mutex->waiters);
    }

    /*
     * Wait loop with timeout check.
     * This is a simplified implementation - a proper one would
     * use a sleep queue with deadline-based wakeup.
     */
    int result = UTHREAD_SUCCESS;
    bool timed_out = false;

    while (self->blocked_queue != NULL) {  /* Still in wait queue */
        preemption_enable();

        /* Check timeout */
        uint64_t now = get_time_ns();
        if (now >= deadline) {
            timed_out = true;
            /* Remove from wait queue */
            preemption_disable();
            wait_queue_remove_specific(cond->waiters, self);
            break;
        }

        /* Yield and wait a bit */
        scheduler_schedule();
        preemption_disable();
    }

    if (timed_out) {
        result = UTHREAD_ETIMEDOUT;
    }

    /* Reacquire the mutex */
    while (mutex->lock != 0) {
        self->state = UTHREAD_STATE_BLOCKED;
        wait_queue_add(mutex->waiters, self);

        preemption_enable();
        scheduler_schedule();
        preemption_disable();
    }

    mutex->lock = 1;
    mutex->owner = self;
    mutex->recursion_count = 1;

    preemption_enable();

    (void)seq;
    return result;
}

int uthread_cond_signal(uthread_cond_t *cond)
{
    if (cond == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized condvar */
    if (!cond->initialized) {
        cond->waiters = malloc(sizeof(struct wait_queue));
        if (cond->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(cond->waiters);
        cond->initialized = true;
    }

    preemption_disable();

    /* Increment sequence number */
    cond->signal_seq++;

    /* Wake one waiting thread */
    if (cond->waiters != NULL && !wait_queue_empty(cond->waiters)) {
        wait_queue_wake_one(cond->waiters);
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_cond_broadcast(uthread_cond_t *cond)
{
    if (cond == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized condvar */
    if (!cond->initialized) {
        cond->waiters = malloc(sizeof(struct wait_queue));
        if (cond->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(cond->waiters);
        cond->initialized = true;
    }

    preemption_disable();

    /* Increment sequence number */
    cond->signal_seq++;

    /* Wake all waiting threads */
    if (cond->waiters != NULL) {
        wait_queue_wake_all(cond->waiters);
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}
