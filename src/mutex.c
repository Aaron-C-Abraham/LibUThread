/**
 * LibUThread Mutex Implementation
 *
 * Blocking mutex with support for normal, recursive, and error-checking types.
 *
 * @file mutex.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Mutex Attribute Functions
 * ========================================================================== */

int uthread_mutexattr_init(uthread_mutexattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    attr->type = UTHREAD_MUTEX_NORMAL;
    return UTHREAD_SUCCESS;
}

int uthread_mutexattr_destroy(uthread_mutexattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Nothing to free */
    return UTHREAD_SUCCESS;
}

int uthread_mutexattr_settype(uthread_mutexattr_t *attr, int type)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    if (type != UTHREAD_MUTEX_NORMAL &&
        type != UTHREAD_MUTEX_RECURSIVE &&
        type != UTHREAD_MUTEX_ERRORCHECK) {
        return UTHREAD_EINVAL;
    }

    attr->type = type;
    return UTHREAD_SUCCESS;
}

int uthread_mutexattr_gettype(const uthread_mutexattr_t *attr, int *type)
{
    if (attr == NULL || type == NULL) {
        return UTHREAD_EINVAL;
    }

    *type = attr->type;
    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Mutex Functions
 * ========================================================================== */

int uthread_mutex_init(uthread_mutex_t *mutex, const uthread_mutexattr_t *attr)
{
    if (mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    memset(mutex, 0, sizeof(*mutex));
    mutex->lock = 0;
    mutex->owner = NULL;
    mutex->recursion_count = 0;

    if (attr != NULL) {
        mutex->type = attr->type;
    } else {
        mutex->type = UTHREAD_MUTEX_NORMAL;
    }

    /* Allocate wait queue */
    mutex->waiters = malloc(sizeof(struct wait_queue));
    if (mutex->waiters == NULL) {
        return UTHREAD_ENOMEM;
    }
    wait_queue_init(mutex->waiters);

    mutex->initialized = true;

    return UTHREAD_SUCCESS;
}

int uthread_mutex_destroy(uthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!mutex->initialized) {
        return UTHREAD_EINVAL;
    }

    /* Cannot destroy a locked mutex */
    if (mutex->lock != 0) {
        return UTHREAD_EBUSY;
    }

    /* Cannot destroy if threads are waiting */
    if (mutex->waiters != NULL && !wait_queue_empty(mutex->waiters)) {
        return UTHREAD_EBUSY;
    }

    /* Free wait queue */
    if (mutex->waiters != NULL) {
        wait_queue_destroy(mutex->waiters);
        free(mutex->waiters);
        mutex->waiters = NULL;
    }

    mutex->initialized = false;

    return UTHREAD_SUCCESS;
}

int uthread_mutex_lock(uthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized mutex (static initializer) */
    if (!mutex->initialized) {
        mutex->waiters = malloc(sizeof(struct wait_queue));
        if (mutex->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(mutex->waiters);
        mutex->initialized = true;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /* Handle recursive/error-checking mutex */
    if (mutex->owner == self) {
        if (mutex->type == UTHREAD_MUTEX_RECURSIVE) {
            mutex->recursion_count++;
            preemption_enable();
            return UTHREAD_SUCCESS;
        } else if (mutex->type == UTHREAD_MUTEX_ERRORCHECK) {
            preemption_enable();
            return UTHREAD_EDEADLK;
        }
        /* Normal mutex: undefined behavior, but we'll deadlock */
    }

    /* Try to acquire lock */
    while (mutex->lock != 0) {
        /* Lock is held by another thread - block */
        if (self != NULL) {
            self->state = UTHREAD_STATE_BLOCKED;
            wait_queue_add(mutex->waiters, self);
        }

        /* Temporarily enable preemption and yield */
        preemption_enable();

        if (self != NULL) {
            /* Schedule another thread - we'll wake up when lock is released */
            scheduler_schedule();
        }

        preemption_disable();
    }

    /* Acquired the lock */
    mutex->lock = 1;
    mutex->owner = self;
    mutex->recursion_count = 1;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_mutex_trylock(uthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized mutex */
    if (!mutex->initialized) {
        mutex->waiters = malloc(sizeof(struct wait_queue));
        if (mutex->waiters == NULL) {
            return UTHREAD_ENOMEM;
        }
        wait_queue_init(mutex->waiters);
        mutex->initialized = true;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /* Handle recursive mutex */
    if (mutex->owner == self) {
        if (mutex->type == UTHREAD_MUTEX_RECURSIVE) {
            mutex->recursion_count++;
            preemption_enable();
            return UTHREAD_SUCCESS;
        } else if (mutex->type == UTHREAD_MUTEX_ERRORCHECK) {
            preemption_enable();
            return UTHREAD_EBUSY;  /* Not EDEADLK for trylock */
        }
    }

    /* Try to acquire */
    if (mutex->lock == 0) {
        mutex->lock = 1;
        mutex->owner = self;
        mutex->recursion_count = 1;
        preemption_enable();
        return UTHREAD_SUCCESS;
    }

    preemption_enable();
    return UTHREAD_EBUSY;
}

int uthread_mutex_unlock(uthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!mutex->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /* Check ownership for error-checking mutex */
    if (mutex->type == UTHREAD_MUTEX_ERRORCHECK) {
        if (mutex->owner != self) {
            preemption_enable();
            return UTHREAD_EPERM;
        }
    }

    /* Handle recursive mutex */
    if (mutex->type == UTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
        mutex->recursion_count--;
        if (mutex->recursion_count > 0) {
            preemption_enable();
            return UTHREAD_SUCCESS;
        }
    }

    /* Release the lock */
    mutex->lock = 0;
    mutex->owner = NULL;
    mutex->recursion_count = 0;

    /* Wake one waiting thread */
    if (mutex->waiters != NULL && !wait_queue_empty(mutex->waiters)) {
        wait_queue_wake_one(mutex->waiters);
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}
