/**
 * LibUThread Read-Write Lock Implementation
 *
 * Allows multiple readers or one writer.
 *
 * @file rwlock.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Read-Write Lock Attribute Functions
 * ========================================================================== */

int uthread_rwlockattr_init(uthread_rwlockattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    attr->prefer_writer = 1;  /* Default: prefer writers */
    return UTHREAD_SUCCESS;
}

int uthread_rwlockattr_destroy(uthread_rwlockattr_t *attr)
{
    if (attr == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Nothing to free */
    return UTHREAD_SUCCESS;
}

/* ==========================================================================
 * Read-Write Lock Functions
 * ========================================================================== */

int uthread_rwlock_init(uthread_rwlock_t *rwlock,
                        const uthread_rwlockattr_t *attr)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    (void)attr;  /* Currently unused */

    memset(rwlock, 0, sizeof(*rwlock));
    rwlock->readers = 0;
    rwlock->writer = 0;
    rwlock->writer_owner = NULL;
    rwlock->pending_writers = 0;

    /* Allocate wait queues */
    rwlock->read_waiters = malloc(sizeof(struct wait_queue));
    if (rwlock->read_waiters == NULL) {
        return UTHREAD_ENOMEM;
    }
    wait_queue_init(rwlock->read_waiters);

    rwlock->write_waiters = malloc(sizeof(struct wait_queue));
    if (rwlock->write_waiters == NULL) {
        free(rwlock->read_waiters);
        return UTHREAD_ENOMEM;
    }
    wait_queue_init(rwlock->write_waiters);

    rwlock->initialized = true;

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_destroy(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!rwlock->initialized) {
        return UTHREAD_EINVAL;
    }

    /* Cannot destroy if in use */
    if (rwlock->readers > 0 || rwlock->writer != 0) {
        return UTHREAD_EBUSY;
    }

    /* Cannot destroy if threads are waiting */
    if (rwlock->read_waiters != NULL && !wait_queue_empty(rwlock->read_waiters)) {
        return UTHREAD_EBUSY;
    }
    if (rwlock->write_waiters != NULL && !wait_queue_empty(rwlock->write_waiters)) {
        return UTHREAD_EBUSY;
    }

    /* Free wait queues */
    if (rwlock->read_waiters != NULL) {
        wait_queue_destroy(rwlock->read_waiters);
        free(rwlock->read_waiters);
        rwlock->read_waiters = NULL;
    }

    if (rwlock->write_waiters != NULL) {
        wait_queue_destroy(rwlock->write_waiters);
        free(rwlock->write_waiters);
        rwlock->write_waiters = NULL;
    }

    rwlock->initialized = false;

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_rdlock(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized rwlock (static initializer) */
    if (!rwlock->initialized) {
        int ret = uthread_rwlock_init(rwlock, NULL);
        if (ret != UTHREAD_SUCCESS) {
            return ret;
        }
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /*
     * Wait while:
     * - A writer holds the lock, OR
     * - Writers are waiting (to prevent writer starvation)
     */
    while (rwlock->writer != 0 || rwlock->pending_writers > 0) {
        if (self != NULL) {
            self->state = UTHREAD_STATE_BLOCKED;
            wait_queue_add(rwlock->read_waiters, self);
        }

        preemption_enable();

        if (self != NULL) {
            scheduler_schedule();
        }

        preemption_disable();
    }

    /* Acquire read lock */
    rwlock->readers++;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_tryrdlock(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized rwlock */
    if (!rwlock->initialized) {
        int ret = uthread_rwlock_init(rwlock, NULL);
        if (ret != UTHREAD_SUCCESS) {
            return ret;
        }
    }

    preemption_disable();

    /* Cannot acquire if writer holds lock or writers waiting */
    if (rwlock->writer != 0 || rwlock->pending_writers > 0) {
        preemption_enable();
        return UTHREAD_EBUSY;
    }

    /* Acquire read lock */
    rwlock->readers++;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_wrlock(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized rwlock */
    if (!rwlock->initialized) {
        int ret = uthread_rwlock_init(rwlock, NULL);
        if (ret != UTHREAD_SUCCESS) {
            return ret;
        }
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    /* Increment pending writers to block new readers */
    rwlock->pending_writers++;

    /*
     * Wait while:
     * - Readers hold the lock, OR
     * - Another writer holds the lock
     */
    while (rwlock->readers > 0 || rwlock->writer != 0) {
        if (self != NULL) {
            self->state = UTHREAD_STATE_BLOCKED;
            wait_queue_add(rwlock->write_waiters, self);
        }

        preemption_enable();

        if (self != NULL) {
            scheduler_schedule();
        }

        preemption_disable();
    }

    /* Decrement pending writers */
    rwlock->pending_writers--;

    /* Acquire write lock */
    rwlock->writer = 1;
    rwlock->writer_owner = self;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_trywrlock(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    /* Handle uninitialized rwlock */
    if (!rwlock->initialized) {
        int ret = uthread_rwlock_init(rwlock, NULL);
        if (ret != UTHREAD_SUCCESS) {
            return ret;
        }
    }

    preemption_disable();

    /* Cannot acquire if any readers or writer */
    if (rwlock->readers > 0 || rwlock->writer != 0) {
        preemption_enable();
        return UTHREAD_EBUSY;
    }

    /* Acquire write lock */
    struct uthread_internal *self = scheduler_current();
    rwlock->writer = 1;
    rwlock->writer_owner = self;

    preemption_enable();

    return UTHREAD_SUCCESS;
}

int uthread_rwlock_unlock(uthread_rwlock_t *rwlock)
{
    if (rwlock == NULL) {
        return UTHREAD_EINVAL;
    }

    if (!rwlock->initialized) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    struct uthread_internal *self = scheduler_current();

    if (rwlock->writer != 0) {
        /* We're releasing a write lock */
        if (rwlock->writer_owner != self) {
            /* Not the owner - error */
            preemption_enable();
            return UTHREAD_EPERM;
        }

        rwlock->writer = 0;
        rwlock->writer_owner = NULL;

        /*
         * Prefer writers: wake one writer if waiting,
         * otherwise wake all readers.
         */
        if (rwlock->write_waiters != NULL &&
            !wait_queue_empty(rwlock->write_waiters)) {
            wait_queue_wake_one(rwlock->write_waiters);
        } else if (rwlock->read_waiters != NULL) {
            wait_queue_wake_all(rwlock->read_waiters);
        }

    } else if (rwlock->readers > 0) {
        /* We're releasing a read lock */
        rwlock->readers--;

        /*
         * If no more readers and writers waiting, wake one writer.
         */
        if (rwlock->readers == 0 &&
            rwlock->write_waiters != NULL &&
            !wait_queue_empty(rwlock->write_waiters)) {
            wait_queue_wake_one(rwlock->write_waiters);
        }

    } else {
        /* Not holding any lock */
        preemption_enable();
        return UTHREAD_EPERM;
    }

    preemption_enable();

    return UTHREAD_SUCCESS;
}
