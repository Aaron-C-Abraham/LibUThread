/**
 * LibUThread Semaphore Implementation
 *
 * Counting semaphores for thread synchronization.
 *
 * @file semaphore.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==========================================================================
 * Semaphore Functions
 * ========================================================================== */

int uthread_sem_init(uthread_sem_t *sem, int pshared, unsigned int value)
{
    if (sem == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Only thread-shared semaphores supported */
    if (pshared != 0) {
        return UTHREAD_EINVAL;
    }

    memset(sem, 0, sizeof(*sem));
    sem->value = (int)value;

    /* Allocate wait queue */
    sem->waiters = malloc(sizeof(struct wait_queue));
    if (sem->waiters == NULL) {
        return UTHREAD_ENOMEM;
    }
    wait_queue_init(sem->waiters);

    sem->initialized = true;

    return UTHREAD_SUCCESS;
}

int uthread_sem_destroy(uthread_sem_t *sem)
{
    if (sem == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    /* Cannot destroy if threads are waiting */
    if (sem->waiters != NULL && !wait_queue_empty(sem->waiters)) {
        return UTHREAD_EBUSY;
    }

    /* Free wait queue */
    if (sem->waiters != NULL) {
        wait_queue_destroy(sem->waiters);
        free(sem->waiters);
        sem->waiters = NULL;
    }

    sem->initialized = false;

    return UTHREAD_SUCCESS;
}

int uthread_sem_wait(uthread_sem_t *sem)
{
    if (sem == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /* Wait while value is 0 */
    while (sem->value <= 0) {
        if (self != NULL) {
            self->state = UTHREAD_STATE_BLOCKED;
            wait_queue_add(sem->waiters, self);
        }

        preemption_enable();

        if (self != NULL) {
            scheduler_schedule();
        }

        preemption_disable();
    }

    /* Decrement the semaphore value */
    sem->value--;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_sem_trywait(uthread_sem_t *sem)
{
    if (sem == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    if (sem->value > 0) {
        sem->value--;
        preemption_enable();
        return UTHREAD_SUCCESS;
    }

    preemption_enable();
    return UTHREAD_EAGAIN;
}

int uthread_sem_timedwait(uthread_sem_t *sem, const struct timespec *abstime)
{
    if (sem == NULL || abstime == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    /* Convert abstime to deadline in nanoseconds */
    uint64_t deadline = (uint64_t)abstime->tv_sec * 1000000000ULL +
                        (uint64_t)abstime->tv_nsec;

    preemption_disable();

    struct uthread_internal *self = scheduler_current();
    int result = UTHREAD_SUCCESS;

    while (sem->value <= 0) {
        /* Check timeout */
        uint64_t now = get_time_ns();
        if (now >= deadline) {
            preemption_enable();
            return UTHREAD_ETIMEDOUT;
        }

        if (self != NULL) {
            self->state = UTHREAD_STATE_BLOCKED;
            wait_queue_add(sem->waiters, self);
        }

        preemption_enable();

        if (self != NULL) {
            /* Yield for a short time */
            scheduler_schedule();
        }

        preemption_disable();

        /* If we timed out while blocked, remove from queue */
        now = get_time_ns();
        if (now >= deadline) {
            if (self != NULL && self->blocked_queue != NULL) {
                wait_queue_remove_specific(sem->waiters, self);
            }
            preemption_enable();
            return UTHREAD_ETIMEDOUT;
        }
    }

    /* Decrement the semaphore value */
    sem->value--;

    preemption_enable();

    return result;
}

int uthread_sem_post(uthread_sem_t *sem)
{
    if (sem == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    /* Increment the semaphore value */
    sem->value++;

    /* Wake one waiting thread if any */
    if (sem->waiters != NULL && !wait_queue_empty(sem->waiters)) {
        wait_queue_wake_one(sem->waiters);
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_sem_getvalue(uthread_sem_t *sem, int *sval)
{
    if (sem == NULL || sval == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!sem->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();
    *sval = sem->value;
    preemption_enable();

    return UTHREAD_SUCCESS;
}
