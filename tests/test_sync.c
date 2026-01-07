/**
 * LibUThread Synchronization Tests
 *
 * Tests for mutex, condition variable, semaphore, and rwlock.
 *
 * @file test_sync.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "uthread.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { \
        test_count++; \
        printf("Test %d: %s... ", test_count, name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        pass_count++; \
        printf("PASSED\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
    } while(0)

/* ==========================================================================
 * Shared Data
 * ========================================================================== */

static uthread_mutex_t g_mutex;
static uthread_cond_t g_cond;
static uthread_sem_t g_sem;
static uthread_rwlock_t g_rwlock;
static int g_shared_counter;
static int g_signal_received;

/* ==========================================================================
 * Mutex Test Functions
 * ========================================================================== */

static void *mutex_increment_thread(void *arg)
{
    int iterations = (int)(intptr_t)arg;

    for (int i = 0; i < iterations; i++) {
        uthread_mutex_lock(&g_mutex);
        g_shared_counter++;
        uthread_mutex_unlock(&g_mutex);
        uthread_yield();  /* Give other threads a chance */
    }

    return NULL;
}

static void *mutex_trylock_thread(void *arg)
{
    int *success = (int *)arg;

    /* Try to lock - should fail if mutex is held */
    int ret = uthread_mutex_trylock(&g_mutex);
    if (ret == 0) {
        *success = 1;
        uthread_mutex_unlock(&g_mutex);
    } else {
        *success = 0;
    }

    return NULL;
}

/* ==========================================================================
 * Condition Variable Test Functions
 * ========================================================================== */

static void *cond_waiter_thread(void *arg)
{
    (void)arg;

    uthread_mutex_lock(&g_mutex);
    while (!g_signal_received) {
        uthread_cond_wait(&g_cond, &g_mutex);
    }
    g_shared_counter++;
    uthread_mutex_unlock(&g_mutex);

    return NULL;
}

static void *cond_signaler_thread(void *arg)
{
    (void)arg;

    uthread_sleep(10);  /* Let waiters get blocked */

    uthread_mutex_lock(&g_mutex);
    g_signal_received = 1;
    uthread_cond_signal(&g_cond);
    uthread_mutex_unlock(&g_mutex);

    return NULL;
}

static void *cond_broadcast_waiter(void *arg)
{
    int *counter = (int *)arg;

    uthread_mutex_lock(&g_mutex);
    while (!g_signal_received) {
        uthread_cond_wait(&g_cond, &g_mutex);
    }
    (*counter)++;
    uthread_mutex_unlock(&g_mutex);

    return NULL;
}

/* ==========================================================================
 * Semaphore Test Functions
 * ========================================================================== */

static void *sem_producer_thread(void *arg)
{
    int count = (int)(intptr_t)arg;

    for (int i = 0; i < count; i++) {
        uthread_sem_post(&g_sem);
        uthread_yield();
    }

    return NULL;
}

static void *sem_consumer_thread(void *arg)
{
    int count = (int)(intptr_t)arg;

    for (int i = 0; i < count; i++) {
        uthread_sem_wait(&g_sem);
        uthread_mutex_lock(&g_mutex);
        g_shared_counter++;
        uthread_mutex_unlock(&g_mutex);
    }

    return NULL;
}

/* ==========================================================================
 * RWLock Test Functions
 * ========================================================================== */

static void *rwlock_reader_thread(void *arg)
{
    int *value_seen = (int *)arg;

    uthread_rwlock_rdlock(&g_rwlock);
    *value_seen = g_shared_counter;
    uthread_sleep(5);  /* Hold lock for a bit */
    uthread_rwlock_unlock(&g_rwlock);

    return NULL;
}

static void *rwlock_writer_thread(void *arg)
{
    int value = (int)(intptr_t)arg;

    uthread_rwlock_wrlock(&g_rwlock);
    g_shared_counter = value;
    uthread_sleep(5);
    uthread_rwlock_unlock(&g_rwlock);

    return NULL;
}

/* ==========================================================================
 * Test Cases
 * ========================================================================== */

void test_mutex_basic(void)
{
    TEST("Mutex basic lock/unlock");

    int ret = uthread_mutex_init(&g_mutex, NULL);
    if (ret != 0) {
        FAIL("uthread_mutex_init failed");
        return;
    }

    ret = uthread_mutex_lock(&g_mutex);
    if (ret != 0) {
        FAIL("uthread_mutex_lock failed");
        return;
    }

    ret = uthread_mutex_unlock(&g_mutex);
    if (ret != 0) {
        FAIL("uthread_mutex_unlock failed");
        return;
    }

    ret = uthread_mutex_destroy(&g_mutex);
    if (ret != 0) {
        FAIL("uthread_mutex_destroy failed");
        return;
    }

    PASS();
}

void test_mutex_contention(void)
{
    TEST("Mutex contention");

    uthread_mutex_init(&g_mutex, NULL);
    g_shared_counter = 0;

    uthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        uthread_create(&threads[i], NULL, mutex_increment_thread, (void *)100);
    }

    for (int i = 0; i < 4; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_mutex_destroy(&g_mutex);

    if (g_shared_counter == 400) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 400, got %d", g_shared_counter);
        FAIL(msg);
    }
}

void test_mutex_trylock(void)
{
    TEST("Mutex trylock");

    uthread_mutex_init(&g_mutex, NULL);

    /* Lock the mutex */
    uthread_mutex_lock(&g_mutex);

    /* Another thread should fail to trylock */
    int success = -1;
    uthread_t thread;
    uthread_create(&thread, NULL, mutex_trylock_thread, &success);
    uthread_join(thread, NULL);

    uthread_mutex_unlock(&g_mutex);

    if (success == 0) {
        /* Now trylock should succeed */
        int ret = uthread_mutex_trylock(&g_mutex);
        if (ret == 0) {
            uthread_mutex_unlock(&g_mutex);
            PASS();
        } else {
            FAIL("trylock should succeed when unlocked");
        }
    } else {
        FAIL("trylock should fail when locked");
    }

    uthread_mutex_destroy(&g_mutex);
}

void test_mutex_recursive(void)
{
    TEST("Recursive mutex");

    uthread_mutexattr_t attr;
    uthread_mutexattr_init(&attr);
    uthread_mutexattr_settype(&attr, UTHREAD_MUTEX_RECURSIVE);

    uthread_mutex_init(&g_mutex, &attr);
    uthread_mutexattr_destroy(&attr);

    /* Lock multiple times */
    int ret = uthread_mutex_lock(&g_mutex);
    ret |= uthread_mutex_lock(&g_mutex);
    ret |= uthread_mutex_lock(&g_mutex);

    if (ret != 0) {
        FAIL("Recursive lock failed");
        return;
    }

    /* Unlock multiple times */
    ret = uthread_mutex_unlock(&g_mutex);
    ret |= uthread_mutex_unlock(&g_mutex);
    ret |= uthread_mutex_unlock(&g_mutex);

    if (ret != 0) {
        FAIL("Recursive unlock failed");
        return;
    }

    uthread_mutex_destroy(&g_mutex);
    PASS();
}

void test_cond_signal(void)
{
    TEST("Condition variable signal");

    uthread_mutex_init(&g_mutex, NULL);
    uthread_cond_init(&g_cond, NULL);
    g_shared_counter = 0;
    g_signal_received = 0;

    uthread_t waiter, signaler;
    uthread_create(&waiter, NULL, cond_waiter_thread, NULL);
    uthread_create(&signaler, NULL, cond_signaler_thread, NULL);

    uthread_join(waiter, NULL);
    uthread_join(signaler, NULL);

    uthread_cond_destroy(&g_cond);
    uthread_mutex_destroy(&g_mutex);

    if (g_shared_counter == 1) {
        PASS();
    } else {
        FAIL("Waiter did not wake up");
    }
}

void test_cond_broadcast(void)
{
    TEST("Condition variable broadcast");

    uthread_mutex_init(&g_mutex, NULL);
    uthread_cond_init(&g_cond, NULL);
    g_signal_received = 0;
    int counter = 0;

    /* Create multiple waiters */
    uthread_t waiters[4];
    for (int i = 0; i < 4; i++) {
        uthread_create(&waiters[i], NULL, cond_broadcast_waiter, &counter);
    }

    /* Give them time to start waiting */
    uthread_sleep(20);

    /* Broadcast to wake all */
    uthread_mutex_lock(&g_mutex);
    g_signal_received = 1;
    uthread_cond_broadcast(&g_cond);
    uthread_mutex_unlock(&g_mutex);

    /* Wait for all to complete */
    for (int i = 0; i < 4; i++) {
        uthread_join(waiters[i], NULL);
    }

    uthread_cond_destroy(&g_cond);
    uthread_mutex_destroy(&g_mutex);

    if (counter == 4) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 4 wakeups, got %d", counter);
        FAIL(msg);
    }
}

void test_semaphore_basic(void)
{
    TEST("Semaphore basic");

    int ret = uthread_sem_init(&g_sem, 0, 0);
    if (ret != 0) {
        FAIL("uthread_sem_init failed");
        return;
    }

    /* trywait should fail on empty semaphore */
    ret = uthread_sem_trywait(&g_sem);
    if (ret == 0) {
        FAIL("trywait should fail on empty semaphore");
        return;
    }

    /* Post to increment */
    ret = uthread_sem_post(&g_sem);
    if (ret != 0) {
        FAIL("uthread_sem_post failed");
        return;
    }

    /* Now trywait should succeed */
    ret = uthread_sem_trywait(&g_sem);
    if (ret != 0) {
        FAIL("trywait should succeed after post");
        return;
    }

    uthread_sem_destroy(&g_sem);
    PASS();
}

void test_semaphore_producer_consumer(void)
{
    TEST("Semaphore producer-consumer");

    uthread_sem_init(&g_sem, 0, 0);
    uthread_mutex_init(&g_mutex, NULL);
    g_shared_counter = 0;

    uthread_t producer, consumer;
    uthread_create(&producer, NULL, sem_producer_thread, (void *)10);
    uthread_create(&consumer, NULL, sem_consumer_thread, (void *)10);

    uthread_join(producer, NULL);
    uthread_join(consumer, NULL);

    uthread_sem_destroy(&g_sem);
    uthread_mutex_destroy(&g_mutex);

    if (g_shared_counter == 10) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 10, got %d", g_shared_counter);
        FAIL(msg);
    }
}

void test_rwlock_basic(void)
{
    TEST("RWLock basic");

    int ret = uthread_rwlock_init(&g_rwlock, NULL);
    if (ret != 0) {
        FAIL("uthread_rwlock_init failed");
        return;
    }

    /* Read lock */
    ret = uthread_rwlock_rdlock(&g_rwlock);
    if (ret != 0) {
        FAIL("rdlock failed");
        return;
    }

    ret = uthread_rwlock_unlock(&g_rwlock);
    if (ret != 0) {
        FAIL("unlock failed");
        return;
    }

    /* Write lock */
    ret = uthread_rwlock_wrlock(&g_rwlock);
    if (ret != 0) {
        FAIL("wrlock failed");
        return;
    }

    ret = uthread_rwlock_unlock(&g_rwlock);
    if (ret != 0) {
        FAIL("unlock failed");
        return;
    }

    uthread_rwlock_destroy(&g_rwlock);
    PASS();
}

void test_rwlock_multiple_readers(void)
{
    TEST("RWLock multiple readers");

    uthread_rwlock_init(&g_rwlock, NULL);
    g_shared_counter = 42;

    int values[3];
    uthread_t readers[3];

    for (int i = 0; i < 3; i++) {
        uthread_create(&readers[i], NULL, rwlock_reader_thread, &values[i]);
    }

    for (int i = 0; i < 3; i++) {
        uthread_join(readers[i], NULL);
    }

    uthread_rwlock_destroy(&g_rwlock);

    /* All readers should see 42 */
    if (values[0] == 42 && values[1] == 42 && values[2] == 42) {
        PASS();
    } else {
        FAIL("Readers saw inconsistent values");
    }
}

void test_rwlock_writer_exclusive(void)
{
    TEST("RWLock writer exclusive");

    uthread_rwlock_init(&g_rwlock, NULL);
    g_shared_counter = 0;

    uthread_t writer1, writer2;
    uthread_create(&writer1, NULL, rwlock_writer_thread, (void *)100);
    uthread_create(&writer2, NULL, rwlock_writer_thread, (void *)200);

    uthread_join(writer1, NULL);
    uthread_join(writer2, NULL);

    uthread_rwlock_destroy(&g_rwlock);

    /* Final value should be either 100 or 200, not something in between */
    if (g_shared_counter == 100 || g_shared_counter == 200) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Unexpected value: %d", g_shared_counter);
        FAIL(msg);
    }
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== LibUThread Synchronization Tests ===\n\n");

    /* Initialize library */
    int ret = uthread_init(SCHED_ROUND_ROBIN);
    if (ret != 0) {
        printf("Failed to initialize library\n");
        return 1;
    }

    test_mutex_basic();
    test_mutex_contention();
    test_mutex_trylock();
    test_mutex_recursive();
    test_cond_signal();
    test_cond_broadcast();
    test_semaphore_basic();
    test_semaphore_producer_consumer();
    test_rwlock_basic();
    test_rwlock_multiple_readers();
    test_rwlock_writer_exclusive();

    uthread_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
