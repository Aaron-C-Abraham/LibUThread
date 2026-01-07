/**
 * LibUThread Stress Tests
 *
 * High-load tests to verify robustness.
 *
 * @file test_stress.c
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

static uthread_mutex_t g_mutex;
static int g_counter;

/* ==========================================================================
 * Test Thread Functions
 * ========================================================================== */

static void *short_thread(void *arg)
{
    (void)arg;
    return NULL;
}

static void *mutex_hammer_thread(void *arg)
{
    int iterations = (int)(intptr_t)arg;

    for (int i = 0; i < iterations; i++) {
        uthread_mutex_lock(&g_mutex);
        g_counter++;
        uthread_mutex_unlock(&g_mutex);
    }

    return NULL;
}

static void *yield_storm_thread(void *arg)
{
    int iterations = (int)(intptr_t)arg;

    for (int i = 0; i < iterations; i++) {
        uthread_yield();
    }

    return NULL;
}

static void *create_child_thread(void *arg)
{
    (void)arg;

    uthread_t child;
    uthread_create(&child, NULL, short_thread, NULL);
    uthread_join(child, NULL);

    return NULL;
}

/* ==========================================================================
 * Stress Tests
 * ========================================================================== */

void test_thread_storm(void)
{
    TEST("Thread storm (100 threads)");

    uthread_init(SCHED_ROUND_ROBIN);

    uthread_t threads[100];
    int created = 0;
    int joined = 0;

    /* Create many short-lived threads */
    for (int i = 0; i < 100; i++) {
        int ret = uthread_create(&threads[i], NULL, short_thread, NULL);
        if (ret == 0) {
            created++;
        }
    }

    /* Join all */
    for (int i = 0; i < created; i++) {
        int ret = uthread_join(threads[i], NULL);
        if (ret == 0) {
            joined++;
        }
    }

    uthread_shutdown();

    if (created == 100 && joined == 100) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "created=%d, joined=%d", created, joined);
        FAIL(msg);
    }
}

void test_mutex_hammer(void)
{
    TEST("Mutex hammer (10 threads, 1000 ops each)");

    uthread_init(SCHED_ROUND_ROBIN);
    uthread_mutex_init(&g_mutex, NULL);
    g_counter = 0;

    uthread_t threads[10];
    for (int i = 0; i < 10; i++) {
        uthread_create(&threads[i], NULL, mutex_hammer_thread, (void *)1000);
    }

    for (int i = 0; i < 10; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_mutex_destroy(&g_mutex);
    uthread_shutdown();

    /* Expected: 10 threads * 1000 = 10000 */
    if (g_counter == 10000) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 10000, got %d", g_counter);
        FAIL(msg);
    }
}

void test_yield_storm(void)
{
    TEST("Yield storm (10 threads, 100 yields each)");

    uthread_init(SCHED_ROUND_ROBIN);

    uthread_t threads[10];
    for (int i = 0; i < 10; i++) {
        uthread_create(&threads[i], NULL, yield_storm_thread, (void *)100);
    }

    int completed = 0;
    for (int i = 0; i < 10; i++) {
        int ret = uthread_join(threads[i], NULL);
        if (ret == 0) {
            completed++;
        }
    }

    uthread_shutdown();

    if (completed == 10) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Only %d completed", completed);
        FAIL(msg);
    }
}

void test_nested_creation(void)
{
    TEST("Nested thread creation");

    uthread_init(SCHED_ROUND_ROBIN);

    uthread_t threads[20];
    int created = 0;

    /* Create threads that create child threads */
    for (int i = 0; i < 20; i++) {
        int ret = uthread_create(&threads[i], NULL, create_child_thread, NULL);
        if (ret == 0) {
            created++;
        }
    }

    int joined = 0;
    for (int i = 0; i < created; i++) {
        int ret = uthread_join(threads[i], NULL);
        if (ret == 0) {
            joined++;
        }
    }

    uthread_shutdown();

    if (created == 20 && joined == 20) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "created=%d, joined=%d", created, joined);
        FAIL(msg);
    }
}

void test_mixed_workload(void)
{
    TEST("Mixed workload");

    uthread_init(SCHED_ROUND_ROBIN);
    uthread_mutex_init(&g_mutex, NULL);
    g_counter = 0;

    /* Mix of different thread behaviors */
    uthread_t threads[30];
    int idx = 0;

    /* 10 short threads */
    for (int i = 0; i < 10; i++) {
        uthread_create(&threads[idx++], NULL, short_thread, NULL);
    }

    /* 10 mutex workers */
    for (int i = 0; i < 10; i++) {
        uthread_create(&threads[idx++], NULL, mutex_hammer_thread, (void *)100);
    }

    /* 10 yielding threads */
    for (int i = 0; i < 10; i++) {
        uthread_create(&threads[idx++], NULL, yield_storm_thread, (void *)50);
    }

    /* Join all */
    int completed = 0;
    for (int i = 0; i < 30; i++) {
        int ret = uthread_join(threads[i], NULL);
        if (ret == 0) {
            completed++;
        }
    }

    uthread_mutex_destroy(&g_mutex);
    uthread_shutdown();

    /* 10 mutex threads * 100 = 1000 */
    if (completed == 30 && g_counter == 1000) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "completed=%d, counter=%d", completed, g_counter);
        FAIL(msg);
    }
}

void test_rapid_init_shutdown(void)
{
    TEST("Rapid init/shutdown cycles");

    for (int cycle = 0; cycle < 5; cycle++) {
        int ret = uthread_init(SCHED_ROUND_ROBIN);
        if (ret != 0) {
            FAIL("init failed");
            return;
        }

        /* Create a few threads each cycle */
        uthread_t threads[5];
        for (int i = 0; i < 5; i++) {
            uthread_create(&threads[i], NULL, short_thread, NULL);
        }

        for (int i = 0; i < 5; i++) {
            uthread_join(threads[i], NULL);
        }

        uthread_shutdown();
    }

    PASS();
}

void test_all_schedulers(void)
{
    TEST("All schedulers work");

    sched_policy_t policies[] = {SCHED_ROUND_ROBIN, SCHED_PRIORITY, SCHED_CFS};
    const char *names[] = {"RR", "Priority", "CFS"};

    for (int p = 0; p < 3; p++) {
        int ret = uthread_init(policies[p]);
        if (ret != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%s init failed", names[p]);
            FAIL(msg);
            return;
        }

        /* Run a simple test */
        g_counter = 0;
        uthread_mutex_init(&g_mutex, NULL);

        uthread_t threads[5];
        for (int i = 0; i < 5; i++) {
            uthread_create(&threads[i], NULL, mutex_hammer_thread, (void *)10);
        }

        for (int i = 0; i < 5; i++) {
            uthread_join(threads[i], NULL);
        }

        uthread_mutex_destroy(&g_mutex);
        uthread_shutdown();

        if (g_counter != 50) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%s: expected 50, got %d", names[p], g_counter);
            FAIL(msg);
            return;
        }
    }

    PASS();
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== LibUThread Stress Tests ===\n\n");

    test_thread_storm();
    test_mutex_hammer();
    test_yield_storm();
    test_nested_creation();
    test_mixed_workload();
    test_rapid_init_shutdown();
    test_all_schedulers();

    printf("\n=== Results: %d/%d tests passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
