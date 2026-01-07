/**
 * LibUThread: Userspace Threading Library with Pluggable Scheduler
 *
 * A complete M:N threading implementation featuring context switching,
 * synchronization primitives, and multiple scheduling algorithms.
 *
 * @file uthread.h
 * @version 1.0.0
 */

#ifndef UTHREAD_H
#define UTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Constants and Limits
 * ========================================================================== */

/** Maximum number of concurrent threads */
#define UTHREAD_MAX_THREADS     1024

/** Default stack size (64 KB) */
#define UTHREAD_STACK_DEFAULT   (64 * 1024)

/** Minimum stack size (16 KB) */
#define UTHREAD_STACK_MIN       (16 * 1024)

/** Maximum stack size (8 MB) */
#define UTHREAD_STACK_MAX       (8 * 1024 * 1024)

/** Maximum thread name length */
#define UTHREAD_NAME_MAX        32

/** Number of priority levels */
#define UTHREAD_PRIORITY_LEVELS 32

/** Default priority (middle) */
#define UTHREAD_PRIORITY_DEFAULT 16

/** Lowest priority */
#define UTHREAD_PRIORITY_MIN    0

/** Highest priority */
#define UTHREAD_PRIORITY_MAX    31

/** Default timeslice in nanoseconds (10 ms) */
#define UTHREAD_TIMESLICE_DEFAULT_NS (10 * 1000 * 1000)

/* ==========================================================================
 * Error Codes
 * ========================================================================== */

/** Success */
#define UTHREAD_SUCCESS         0

/** Invalid argument */
#define UTHREAD_EINVAL          22

/** Not enough memory */
#define UTHREAD_ENOMEM          12

/** Resource busy */
#define UTHREAD_EBUSY           16

/** Deadlock would occur */
#define UTHREAD_EDEADLK         35

/** Operation not permitted */
#define UTHREAD_EPERM           1

/** Timed out */
#define UTHREAD_ETIMEDOUT       110

/** Resource temporarily unavailable */
#define UTHREAD_EAGAIN          11

/** No such thread */
#define UTHREAD_ESRCH           3

/* ==========================================================================
 * Type Definitions
 * ========================================================================== */

/** Thread handle (opaque) */
typedef struct uthread *uthread_t;

/** Scheduling policy */
typedef enum sched_policy {
    SCHED_ROUND_ROBIN = 0,      /**< Round-robin scheduling */
    SCHED_PRIORITY    = 1,      /**< Priority-based scheduling */
    SCHED_CFS         = 2       /**< Completely Fair Scheduler */
} sched_policy_t;

/** Thread state */
typedef enum uthread_state {
    UTHREAD_STATE_READY      = 0,   /**< Ready to run */
    UTHREAD_STATE_RUNNING    = 1,   /**< Currently running */
    UTHREAD_STATE_BLOCKED    = 2,   /**< Blocked on synchronization */
    UTHREAD_STATE_TERMINATED = 3,   /**< Finished execution */
    UTHREAD_STATE_JOINABLE   = 4    /**< Terminated, waiting for join */
} uthread_state_t;

/** Detach state for thread attributes */
typedef enum uthread_detachstate {
    UTHREAD_CREATE_JOINABLE = 0,    /**< Thread is joinable (default) */
    UTHREAD_CREATE_DETACHED = 1     /**< Thread is detached */
} uthread_detachstate_t;

/** Mutex type */
typedef enum uthread_mutex_type {
    UTHREAD_MUTEX_NORMAL     = 0,   /**< Normal mutex (default) */
    UTHREAD_MUTEX_RECURSIVE  = 1,   /**< Recursive mutex */
    UTHREAD_MUTEX_ERRORCHECK = 2    /**< Error-checking mutex */
} uthread_mutex_type_t;

/* ==========================================================================
 * Thread Attributes
 * ========================================================================== */

/** Thread attributes structure */
typedef struct uthread_attr {
    size_t stack_size;              /**< Stack size in bytes */
    int priority;                   /**< Thread priority (0-31) */
    int nice;                       /**< Nice value for CFS (-20 to +19) */
    uthread_detachstate_t detach_state; /**< Joinable or detached */
    char name[UTHREAD_NAME_MAX];    /**< Optional thread name */
} uthread_attr_t;

/* ==========================================================================
 * Synchronization Primitives
 * ========================================================================== */

/** Forward declaration of wait queue */
struct wait_queue;

/** Mutex structure */
typedef struct uthread_mutex {
    volatile int lock;              /**< Lock state: 0=unlocked, 1=locked */
    uthread_t owner;                /**< Thread holding the lock */
    struct wait_queue *waiters;     /**< Queue of waiting threads */
    uthread_mutex_type_t type;      /**< Mutex type */
    int recursion_count;            /**< Recursion count for recursive mutex */
    bool initialized;               /**< True if properly initialized */
} uthread_mutex_t;

/** Mutex attributes */
typedef struct uthread_mutexattr {
    uthread_mutex_type_t type;      /**< Mutex type */
} uthread_mutexattr_t;

/** Condition variable structure */
typedef struct uthread_cond {
    struct wait_queue *waiters;     /**< Queue of waiting threads */
    uint64_t signal_seq;            /**< Signal sequence number */
    bool initialized;               /**< True if properly initialized */
} uthread_cond_t;

/** Condition variable attributes */
typedef struct uthread_condattr {
    int clock_id;                   /**< Clock for timed waits */
} uthread_condattr_t;

/** Semaphore structure */
typedef struct uthread_sem {
    volatile int value;             /**< Current value */
    struct wait_queue *waiters;     /**< Queue of waiting threads */
    bool initialized;               /**< True if properly initialized */
} uthread_sem_t;

/** Read-Write lock structure */
typedef struct uthread_rwlock {
    volatile int readers;           /**< Number of active readers */
    volatile int writer;            /**< 1 if writer holds lock */
    uthread_t writer_owner;         /**< Writer thread */
    struct wait_queue *read_waiters;  /**< Waiting readers */
    struct wait_queue *write_waiters; /**< Waiting writers */
    int pending_writers;            /**< Count of pending writers */
    bool initialized;               /**< True if properly initialized */
} uthread_rwlock_t;

/** Read-Write lock attributes */
typedef struct uthread_rwlockattr {
    int prefer_writer;              /**< Prefer writers over readers */
} uthread_rwlockattr_t;

/* ==========================================================================
 * Static Initializers
 * ========================================================================== */

/** Static initializer for mutex */
#define UTHREAD_MUTEX_INITIALIZER { \
    .lock = 0, \
    .owner = NULL, \
    .waiters = NULL, \
    .type = UTHREAD_MUTEX_NORMAL, \
    .recursion_count = 0, \
    .initialized = true \
}

/** Static initializer for condition variable */
#define UTHREAD_COND_INITIALIZER { \
    .waiters = NULL, \
    .signal_seq = 0, \
    .initialized = true \
}

/** Static initializer for rwlock */
#define UTHREAD_RWLOCK_INITIALIZER { \
    .readers = 0, \
    .writer = 0, \
    .writer_owner = NULL, \
    .read_waiters = NULL, \
    .write_waiters = NULL, \
    .pending_writers = 0, \
    .initialized = true \
}

/* ==========================================================================
 * Library Initialization
 * ========================================================================== */

/**
 * Initialize the threading library.
 * Must be called before any other uthread function.
 *
 * @param policy Scheduling policy to use
 * @return 0 on success, error code on failure
 */
int uthread_init(sched_policy_t policy);

/**
 * Shutdown the threading library.
 * Waits for all threads to terminate.
 */
void uthread_shutdown(void);

/**
 * Check if the library is initialized.
 *
 * @return true if initialized, false otherwise
 */
bool uthread_is_initialized(void);

/**
 * Get the current scheduling policy.
 *
 * @return Current scheduling policy
 */
sched_policy_t uthread_get_policy(void);

/* ==========================================================================
 * Thread Management
 * ========================================================================== */

/**
 * Create a new thread.
 *
 * @param thread    Pointer to store thread handle
 * @param attr      Thread attributes (NULL for defaults)
 * @param start     Thread entry function
 * @param arg       Argument passed to start function
 * @return 0 on success, error code on failure
 */
int uthread_create(uthread_t *thread,
                   const uthread_attr_t *attr,
                   void *(*start)(void *),
                   void *arg);

/**
 * Wait for thread termination.
 *
 * @param thread    Thread to wait for
 * @param retval    Pointer to store return value (NULL to ignore)
 * @return 0 on success, error code on failure
 */
int uthread_join(uthread_t thread, void **retval);

/**
 * Detach a thread (resources freed on exit).
 *
 * @param thread    Thread to detach
 * @return 0 on success, error code on failure
 */
int uthread_detach(uthread_t thread);

/**
 * Voluntarily yield CPU to another thread.
 */
void uthread_yield(void);

/**
 * Terminate calling thread.
 *
 * @param retval    Return value for joining thread
 */
void uthread_exit(void *retval) __attribute__((noreturn));

/**
 * Get handle of calling thread.
 *
 * @return Thread handle
 */
uthread_t uthread_self(void);

/**
 * Compare two thread handles.
 *
 * @param t1 First thread handle
 * @param t2 Second thread handle
 * @return Non-zero if equal, 0 otherwise
 */
int uthread_equal(uthread_t t1, uthread_t t2);

/**
 * Sleep for specified duration.
 *
 * @param milliseconds Sleep duration in milliseconds
 */
void uthread_sleep(unsigned int milliseconds);

/**
 * Get the thread ID.
 *
 * @param thread Thread handle
 * @return Thread ID, or -1 if invalid
 */
int uthread_get_tid(uthread_t thread);

/**
 * Set thread name (for debugging).
 *
 * @param thread Thread handle
 * @param name   Thread name
 * @return 0 on success, error code on failure
 */
int uthread_setname(uthread_t thread, const char *name);

/**
 * Get thread name.
 *
 * @param thread Thread handle
 * @param name   Buffer to store name
 * @param len    Buffer length
 * @return 0 on success, error code on failure
 */
int uthread_getname(uthread_t thread, char *name, size_t len);

/* ==========================================================================
 * Thread Attributes
 * ========================================================================== */

/**
 * Initialize thread attributes to defaults.
 *
 * @param attr Attribute structure to initialize
 * @return 0 on success, error code on failure
 */
int uthread_attr_init(uthread_attr_t *attr);

/**
 * Destroy thread attributes.
 *
 * @param attr Attribute structure to destroy
 * @return 0 on success, error code on failure
 */
int uthread_attr_destroy(uthread_attr_t *attr);

/**
 * Set stack size for new threads.
 *
 * @param attr Attribute object
 * @param size Stack size in bytes (minimum 16KB)
 * @return 0 on success, error code on failure
 */
int uthread_attr_setstacksize(uthread_attr_t *attr, size_t size);

/**
 * Get stack size from attributes.
 *
 * @param attr Attribute object
 * @param size Pointer to store stack size
 * @return 0 on success, error code on failure
 */
int uthread_attr_getstacksize(const uthread_attr_t *attr, size_t *size);

/**
 * Set thread priority (for priority scheduler).
 *
 * @param attr     Attribute object
 * @param priority Priority value (0=lowest, 31=highest)
 * @return 0 on success, error code on failure
 */
int uthread_attr_setpriority(uthread_attr_t *attr, int priority);

/**
 * Get thread priority from attributes.
 *
 * @param attr     Attribute object
 * @param priority Pointer to store priority
 * @return 0 on success, error code on failure
 */
int uthread_attr_getpriority(const uthread_attr_t *attr, int *priority);

/**
 * Set nice value (for CFS scheduler).
 *
 * @param attr Attribute object
 * @param nice Nice value (-20 to +19)
 * @return 0 on success, error code on failure
 */
int uthread_attr_setnice(uthread_attr_t *attr, int nice);

/**
 * Get nice value from attributes.
 *
 * @param attr Attribute object
 * @param nice Pointer to store nice value
 * @return 0 on success, error code on failure
 */
int uthread_attr_getnice(const uthread_attr_t *attr, int *nice);

/**
 * Set detached state.
 *
 * @param attr        Attribute object
 * @param detachstate UTHREAD_CREATE_JOINABLE or UTHREAD_CREATE_DETACHED
 * @return 0 on success, error code on failure
 */
int uthread_attr_setdetachstate(uthread_attr_t *attr, int detachstate);

/**
 * Get detached state from attributes.
 *
 * @param attr        Attribute object
 * @param detachstate Pointer to store detach state
 * @return 0 on success, error code on failure
 */
int uthread_attr_getdetachstate(const uthread_attr_t *attr, int *detachstate);

/**
 * Set thread name in attributes.
 *
 * @param attr Attribute object
 * @param name Thread name
 * @return 0 on success, error code on failure
 */
int uthread_attr_setname(uthread_attr_t *attr, const char *name);

/* ==========================================================================
 * Mutex Operations
 * ========================================================================== */

/**
 * Initialize mutex with given attributes.
 *
 * @param mutex Mutex to initialize
 * @param attr  Mutex attributes (NULL for defaults)
 * @return 0 on success, error code on failure
 */
int uthread_mutex_init(uthread_mutex_t *mutex,
                       const uthread_mutexattr_t *attr);

/**
 * Destroy mutex.
 *
 * @param mutex Mutex to destroy
 * @return 0 on success, error code on failure
 */
int uthread_mutex_destroy(uthread_mutex_t *mutex);

/**
 * Lock mutex (blocking).
 * Blocks if mutex is held by another thread.
 *
 * @param mutex Mutex to lock
 * @return 0 on success, error code on failure
 */
int uthread_mutex_lock(uthread_mutex_t *mutex);

/**
 * Try to lock mutex (non-blocking).
 *
 * @param mutex Mutex to lock
 * @return 0 if locked, UTHREAD_EBUSY if already held
 */
int uthread_mutex_trylock(uthread_mutex_t *mutex);

/**
 * Unlock mutex.
 * Must be called by thread that holds the lock.
 *
 * @param mutex Mutex to unlock
 * @return 0 on success, error code on failure
 */
int uthread_mutex_unlock(uthread_mutex_t *mutex);

/**
 * Initialize mutex attributes.
 *
 * @param attr Attribute structure to initialize
 * @return 0 on success, error code on failure
 */
int uthread_mutexattr_init(uthread_mutexattr_t *attr);

/**
 * Destroy mutex attributes.
 *
 * @param attr Attribute structure to destroy
 * @return 0 on success, error code on failure
 */
int uthread_mutexattr_destroy(uthread_mutexattr_t *attr);

/**
 * Set mutex type.
 *
 * @param attr Attribute object
 * @param type Mutex type
 * @return 0 on success, error code on failure
 */
int uthread_mutexattr_settype(uthread_mutexattr_t *attr, int type);

/**
 * Get mutex type.
 *
 * @param attr Attribute object
 * @param type Pointer to store type
 * @return 0 on success, error code on failure
 */
int uthread_mutexattr_gettype(const uthread_mutexattr_t *attr, int *type);

/* ==========================================================================
 * Condition Variable Operations
 * ========================================================================== */

/**
 * Initialize condition variable.
 *
 * @param cond Condition variable to initialize
 * @param attr Condition variable attributes (NULL for defaults)
 * @return 0 on success, error code on failure
 */
int uthread_cond_init(uthread_cond_t *cond,
                      const uthread_condattr_t *attr);

/**
 * Destroy condition variable.
 *
 * @param cond Condition variable to destroy
 * @return 0 on success, error code on failure
 */
int uthread_cond_destroy(uthread_cond_t *cond);

/**
 * Wait on condition variable.
 * Atomically releases mutex and blocks.
 * Reacquires mutex before returning.
 *
 * @param cond  Condition variable
 * @param mutex Associated mutex (must be locked)
 * @return 0 on success, error code on failure
 */
int uthread_cond_wait(uthread_cond_t *cond,
                      uthread_mutex_t *mutex);

/**
 * Timed wait on condition variable.
 *
 * @param cond    Condition variable
 * @param mutex   Associated mutex (must be locked)
 * @param abstime Absolute timeout (CLOCK_MONOTONIC)
 * @return 0 on signal, UTHREAD_ETIMEDOUT on timeout
 */
int uthread_cond_timedwait(uthread_cond_t *cond,
                           uthread_mutex_t *mutex,
                           const struct timespec *abstime);

/**
 * Wake one waiting thread.
 *
 * @param cond Condition variable
 * @return 0 on success, error code on failure
 */
int uthread_cond_signal(uthread_cond_t *cond);

/**
 * Wake all waiting threads.
 *
 * @param cond Condition variable
 * @return 0 on success, error code on failure
 */
int uthread_cond_broadcast(uthread_cond_t *cond);

/**
 * Initialize condition variable attributes.
 *
 * @param attr Attribute structure to initialize
 * @return 0 on success, error code on failure
 */
int uthread_condattr_init(uthread_condattr_t *attr);

/**
 * Destroy condition variable attributes.
 *
 * @param attr Attribute structure to destroy
 * @return 0 on success, error code on failure
 */
int uthread_condattr_destroy(uthread_condattr_t *attr);

/* ==========================================================================
 * Semaphore Operations
 * ========================================================================== */

/**
 * Initialize semaphore.
 *
 * @param sem     Semaphore to initialize
 * @param pshared 0 for thread-shared (only supported value)
 * @param value   Initial value
 * @return 0 on success, error code on failure
 */
int uthread_sem_init(uthread_sem_t *sem, int pshared, unsigned int value);

/**
 * Destroy semaphore.
 *
 * @param sem Semaphore to destroy
 * @return 0 on success, error code on failure
 */
int uthread_sem_destroy(uthread_sem_t *sem);

/**
 * Decrement semaphore (P/wait operation).
 * Blocks if value is 0.
 *
 * @param sem Semaphore
 * @return 0 on success, error code on failure
 */
int uthread_sem_wait(uthread_sem_t *sem);

/**
 * Try decrement semaphore (non-blocking).
 *
 * @param sem Semaphore
 * @return 0 on success, UTHREAD_EAGAIN if would block
 */
int uthread_sem_trywait(uthread_sem_t *sem);

/**
 * Timed wait on semaphore.
 *
 * @param sem     Semaphore
 * @param abstime Absolute timeout
 * @return 0 on success, UTHREAD_ETIMEDOUT on timeout
 */
int uthread_sem_timedwait(uthread_sem_t *sem, const struct timespec *abstime);

/**
 * Increment semaphore (V/post operation).
 * Wakes one waiting thread if any.
 *
 * @param sem Semaphore
 * @return 0 on success, error code on failure
 */
int uthread_sem_post(uthread_sem_t *sem);

/**
 * Get current semaphore value.
 *
 * @param sem  Semaphore
 * @param sval Pointer to store value
 * @return 0 on success, error code on failure
 */
int uthread_sem_getvalue(uthread_sem_t *sem, int *sval);

/* ==========================================================================
 * Read-Write Lock Operations
 * ========================================================================== */

/**
 * Initialize read-write lock.
 *
 * @param rwlock Read-write lock to initialize
 * @param attr   Attributes (NULL for defaults)
 * @return 0 on success, error code on failure
 */
int uthread_rwlock_init(uthread_rwlock_t *rwlock,
                        const uthread_rwlockattr_t *attr);

/**
 * Destroy read-write lock.
 *
 * @param rwlock Read-write lock to destroy
 * @return 0 on success, error code on failure
 */
int uthread_rwlock_destroy(uthread_rwlock_t *rwlock);

/**
 * Acquire read lock.
 * Multiple readers allowed, blocks if writer holds lock.
 *
 * @param rwlock Read-write lock
 * @return 0 on success, error code on failure
 */
int uthread_rwlock_rdlock(uthread_rwlock_t *rwlock);

/**
 * Try acquire read lock (non-blocking).
 *
 * @param rwlock Read-write lock
 * @return 0 on success, UTHREAD_EBUSY if locked
 */
int uthread_rwlock_tryrdlock(uthread_rwlock_t *rwlock);

/**
 * Acquire write lock.
 * Exclusive access, blocks if any reader or writer holds lock.
 *
 * @param rwlock Read-write lock
 * @return 0 on success, error code on failure
 */
int uthread_rwlock_wrlock(uthread_rwlock_t *rwlock);

/**
 * Try acquire write lock (non-blocking).
 *
 * @param rwlock Read-write lock
 * @return 0 on success, UTHREAD_EBUSY if locked
 */
int uthread_rwlock_trywrlock(uthread_rwlock_t *rwlock);

/**
 * Release read or write lock.
 *
 * @param rwlock Read-write lock
 * @return 0 on success, error code on failure
 */
int uthread_rwlock_unlock(uthread_rwlock_t *rwlock);

/**
 * Initialize read-write lock attributes.
 *
 * @param attr Attribute structure to initialize
 * @return 0 on success, error code on failure
 */
int uthread_rwlockattr_init(uthread_rwlockattr_t *attr);

/**
 * Destroy read-write lock attributes.
 *
 * @param attr Attribute structure to destroy
 * @return 0 on success, error code on failure
 */
int uthread_rwlockattr_destroy(uthread_rwlockattr_t *attr);

/* ==========================================================================
 * Scheduler Control (Advanced)
 * ========================================================================== */

/**
 * Set the timeslice duration (affects preemption).
 *
 * @param ns Timeslice in nanoseconds
 * @return 0 on success, error code on failure
 */
int uthread_set_timeslice(uint64_t ns);

/**
 * Get the current timeslice duration.
 *
 * @return Timeslice in nanoseconds
 */
uint64_t uthread_get_timeslice(void);

/**
 * Enable or disable preemption.
 *
 * @param enable true to enable, false to disable
 * @return Previous state
 */
bool uthread_set_preemption(bool enable);

/**
 * Change thread priority at runtime.
 *
 * @param thread   Thread to modify
 * @param priority New priority (0-31)
 * @return 0 on success, error code on failure
 */
int uthread_setpriority(uthread_t thread, int priority);

/**
 * Get thread priority.
 *
 * @param thread   Thread to query
 * @param priority Pointer to store priority
 * @return 0 on success, error code on failure
 */
int uthread_getpriority(uthread_t thread, int *priority);

/**
 * Change thread nice value at runtime.
 *
 * @param thread Thread to modify
 * @param nice   New nice value (-20 to +19)
 * @return 0 on success, error code on failure
 */
int uthread_setnice(uthread_t thread, int nice);

/**
 * Get thread nice value.
 *
 * @param thread Thread to query
 * @param nice   Pointer to store nice value
 * @return 0 on success, error code on failure
 */
int uthread_getnice(uthread_t thread, int *nice);

/* ==========================================================================
 * Statistics and Debugging
 * ========================================================================== */

/** Thread statistics structure */
typedef struct uthread_stats {
    int total_threads;              /**< Total threads created */
    int active_threads;             /**< Currently active threads */
    int ready_threads;              /**< Threads in ready queue */
    int blocked_threads;            /**< Blocked threads */
    uint64_t context_switches;      /**< Total context switches */
    uint64_t scheduler_invocations; /**< Scheduler calls */
    uint64_t total_runtime_ns;      /**< Total runtime */
} uthread_stats_t;

/**
 * Get library statistics.
 *
 * @param stats Pointer to store statistics
 * @return 0 on success, error code on failure
 */
int uthread_get_stats(uthread_stats_t *stats);

/**
 * Reset statistics counters.
 */
void uthread_reset_stats(void);

/**
 * Print debug information about all threads.
 */
void uthread_debug_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* UTHREAD_H */
