/**
 * LibUThread Scheduler Tests
 *
 * Tests for different scheduling algorithms.
 *
 * @file test_scheduler.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * Test Variables
 * ========================================================================== */

static int g_execution_order[10];
static int g_order_index = 0;
static uthread_mutex_t g_order_mutex;

/* ==========================================================================
 * Test Thread Functions
 * ========================================================================== */

static void *record_order_thread(void *arg)
{
    int id = (int)(intptr_t)arg;

    uthread_mutex_lock(&g_order_mutex);
    if (g_order_index < 10) {
        g_execution_order[g_order_index++] = id;
    }
    uthread_mutex_unlock(&g_order_mutex);

    return NULL;
}

static void *priority_worker_thread(void *arg)
{
    int id = (int)(intptr_t)arg;

    /* Do some work */
    for (volatile int i = 0; i < 1000; i++) {
        /* Busy work */
    }

    uthread_mutex_lock(&g_order_mutex);
    if (g_order_index < 10) {
        g_execution_order[g_order_index++] = id;
    }
    uthread_mutex_unlock(&g_order_mutex);

    return NULL;
}

static void *fairness_thread(void *arg)
{
    int *counter = (int *)arg;

    for (int i = 0; i < 100; i++) {
        (*counter)++;
        uthread_yield();
    }

    return NULL;
}

/* ==========================================================================
 * Round-Robin Tests
 * ========================================================================== */

void test_rr_basic(void)
{
    TEST("RR: Basic scheduling");

    uthread_init(SCHED_ROUND_ROBIN);
    uthread_mutex_init(&g_order_mutex, NULL);
    g_order_index = 0;

    uthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        uthread_create(&threads[i], NULL, record_order_thread, (void *)(intptr_t)i);
    }

    for (int i = 0; i < 3; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_mutex_destroy(&g_order_mutex);
    uthread_shutdown();

    /* All threads should have executed */
    if (g_order_index == 3) {
        PASS();
    } else {
        FAIL("Not all threads recorded");
    }
}

void test_rr_fairness(void)
{
    TEST("RR: Fairness");

    uthread_init(SCHED_ROUND_ROBIN);

    int counters[3] = {0, 0, 0};
    uthread_t threads[3];

    for (int i = 0; i < 3; i++) {
        uthread_create(&threads[i], NULL, fairness_thread, &counters[i]);
    }

    for (int i = 0; i < 3; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_shutdown();

    /* All counters should be equal (100 each) */
    if (counters[0] == 100 && counters[1] == 100 && counters[2] == 100) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Counters: %d, %d, %d",
                 counters[0], counters[1], counters[2]);
        FAIL(msg);
    }
}

/* ==========================================================================
 * Priority Scheduler Tests
 * ========================================================================== */

void test_priority_basic(void)
{
    TEST("Priority: Basic scheduling");

    uthread_init(SCHED_PRIORITY);
    uthread_mutex_init(&g_order_mutex, NULL);
    g_order_index = 0;

    uthread_t threads[3];
    uthread_attr_t attr;

    /* Create threads with different priorities */
    /* Higher priority threads should run first */
    int priorities[3] = {10, 20, 30};  /* 30 is highest */

    for (int i = 0; i < 3; i++) {
        uthread_attr_init(&attr);
        uthread_attr_setpriority(&attr, priorities[i]);
        uthread_create(&threads[i], &attr, priority_worker_thread, (void *)(intptr_t)i);
        uthread_attr_destroy(&attr);
    }

    for (int i = 0; i < 3; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_mutex_destroy(&g_order_mutex);
    uthread_shutdown();

    /* Thread 2 (priority 30) should complete first, then 1, then 0 */
    if (g_order_index == 3) {
        /* Priority order: 2, 1, 0 */
        if (g_execution_order[0] == 2 &&
            g_execution_order[1] == 1 &&
            g_execution_order[2] == 0) {
            PASS();
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "Order: %d, %d, %d (expected 2, 1, 0)",
                     g_execution_order[0], g_execution_order[1], g_execution_order[2]);
            FAIL(msg);
        }
    } else {
        FAIL("Not all threads completed");
    }
}

void test_priority_change(void)
{
    TEST("Priority: Dynamic priority change");

    uthread_init(SCHED_PRIORITY);

    /* Get current thread and change its priority */
    uthread_t self = uthread_self();
    int original_priority, new_priority;

    uthread_getpriority(self, &original_priority);
    uthread_setpriority(self, 25);
    uthread_getpriority(self, &new_priority);

    uthread_shutdown();

    if (new_priority == 25) {
        PASS();
    } else {
        FAIL("Priority change failed");
    }
}

/* ==========================================================================
 * CFS Scheduler Tests
 * ========================================================================== */

void test_cfs_basic(void)
{
    TEST("CFS: Basic scheduling");

    uthread_init(SCHED_CFS);
    uthread_mutex_init(&g_order_mutex, NULL);
    g_order_index = 0;

    uthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        uthread_create(&threads[i], NULL, record_order_thread, (void *)(intptr_t)i);
    }

    for (int i = 0; i < 3; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_mutex_destroy(&g_order_mutex);
    uthread_shutdown();

    if (g_order_index == 3) {
        PASS();
    } else {
        FAIL("Not all threads completed");
    }
}

void test_cfs_nice_values(void)
{
    TEST("CFS: Nice value fairness");

    uthread_init(SCHED_CFS);

    int counters[2] = {0, 0};
    uthread_t threads[2];
    uthread_attr_t attr;

    /* Thread 0: nice 0 (normal) */
    /* Thread 1: nice 10 (lower priority) */
    int nice_values[2] = {0, 10};

    for (int i = 0; i < 2; i++) {
        uthread_attr_init(&attr);
        uthread_attr_setnice(&attr, nice_values[i]);
        uthread_create(&threads[i], &attr, fairness_thread, &counters[i]);
        uthread_attr_destroy(&attr);
    }

    for (int i = 0; i < 2; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_shutdown();

    /* Both should complete their iterations */
    if (counters[0] == 100 && counters[1] == 100) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Counters: %d, %d",
                 counters[0], counters[1]);
        FAIL(msg);
    }
}

/* ==========================================================================
 * Timeslice Tests
 * ========================================================================== */

void test_timeslice_config(void)
{
    TEST("Timeslice configuration");

    uthread_init(SCHED_ROUND_ROBIN);

    uint64_t original = uthread_get_timeslice();
    uint64_t new_slice = 5 * 1000 * 1000;  /* 5ms */

    int ret = uthread_set_timeslice(new_slice);
    if (ret != 0) {
        FAIL("uthread_set_timeslice failed");
        uthread_shutdown();
        return;
    }

    uint64_t retrieved = uthread_get_timeslice();

    uthread_shutdown();

    if (retrieved == new_slice) {
        PASS();
    } else {
        FAIL("Timeslice not set correctly");
    }
}

/* ==========================================================================
 * Statistics Tests
 * ========================================================================== */

void test_statistics(void)
{
    TEST("Statistics collection");

    uthread_init(SCHED_ROUND_ROBIN);

    /* Create and run some threads */
    int counter = 0;
    uthread_t threads[5];

    for (int i = 0; i < 5; i++) {
        uthread_create(&threads[i], NULL, fairness_thread, &counter);
    }

    for (int i = 0; i < 5; i++) {
        uthread_join(threads[i], NULL);
    }

    /* Get statistics */
    uthread_stats_t stats;
    uthread_get_stats(&stats);

    uthread_shutdown();

    /* Should have created at least 6 threads (5 + main) */
    if (stats.total_threads >= 5 && stats.context_switches > 0) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "total=%d, switches=%lu",
                 stats.total_threads, (unsigned long)stats.context_switches);
        FAIL(msg);
    }
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== LibUThread Scheduler Tests ===\n\n");

    /* Round-Robin tests */
    test_rr_basic();
    test_rr_fairness();

    /* Priority scheduler tests */
    test_priority_basic();
    test_priority_change();

    /* CFS tests */
    test_cfs_basic();
    test_cfs_nice_values();

    /* Configuration tests */
    test_timeslice_config();
    test_statistics();

    printf("\n=== Results: %d/%d tests passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
