/**
 * LibUThread Basic Tests
 *
 * Tests for basic thread creation, joining, yielding, etc.
 *
 * @file test_basic.c
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
 * Test Thread Functions
 * ========================================================================== */

static void *simple_thread(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
    return NULL;
}

static void *return_value_thread(void *arg)
{
    return (void *)((intptr_t)arg * 2);
}

static void *yield_thread(void *arg)
{
    int *counter = (int *)arg;
    for (int i = 0; i < 5; i++) {
        (*counter)++;
        uthread_yield();
    }
    return NULL;
}

static void *sleep_thread(void *arg)
{
    int *counter = (int *)arg;
    uthread_sleep(10);  /* Sleep 10ms */
    (*counter)++;
    return NULL;
}

static void *self_thread(void *arg)
{
    uthread_t *result = (uthread_t *)arg;
    *result = uthread_self();
    return NULL;
}

static void *exit_thread(void *arg)
{
    uthread_exit((void *)42);
    /* Should not reach here */
    *(int *)arg = -1;
    return NULL;
}

/* ==========================================================================
 * Test Cases
 * ========================================================================== */

void test_init(void)
{
    TEST("uthread_init with Round-Robin");
    int ret = uthread_init(SCHED_ROUND_ROBIN);
    if (ret == 0 && uthread_is_initialized()) {
        PASS();
    } else {
        FAIL("Failed to initialize");
    }
}

void test_create_single(void)
{
    TEST("Create single thread");
    int counter = 0;
    uthread_t thread;

    int ret = uthread_create(&thread, NULL, simple_thread, &counter);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread, NULL);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if (counter == 1) {
        PASS();
    } else {
        FAIL("Thread did not execute");
    }
}

void test_create_many(void)
{
    TEST("Create multiple threads");
    int counter = 0;
    uthread_t threads[10];

    for (int i = 0; i < 10; i++) {
        int ret = uthread_create(&threads[i], NULL, simple_thread, &counter);
        if (ret != 0) {
            FAIL("uthread_create failed");
            return;
        }
    }

    for (int i = 0; i < 10; i++) {
        int ret = uthread_join(threads[i], NULL);
        if (ret != 0) {
            FAIL("uthread_join failed");
            return;
        }
    }

    if (counter == 10) {
        PASS();
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 10, got %d", counter);
        FAIL(msg);
    }
}

void test_return_value(void)
{
    TEST("Thread return value");
    uthread_t thread;
    void *retval;

    int ret = uthread_create(&thread, NULL, return_value_thread, (void *)21);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread, &retval);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if ((intptr_t)retval == 42) {
        PASS();
    } else {
        FAIL("Wrong return value");
    }
}

void test_yield(void)
{
    TEST("Thread yield");
    int counter1 = 0, counter2 = 0;
    uthread_t thread1, thread2;

    int ret = uthread_create(&thread1, NULL, yield_thread, &counter1);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_create(&thread2, NULL, yield_thread, &counter2);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread1, NULL);
    ret |= uthread_join(thread2, NULL);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if (counter1 == 5 && counter2 == 5) {
        PASS();
    } else {
        FAIL("Threads did not complete");
    }
}

void test_self(void)
{
    TEST("uthread_self");
    uthread_t thread, self_result = NULL;

    int ret = uthread_create(&thread, NULL, self_thread, &self_result);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread, NULL);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if (uthread_equal(thread, self_result)) {
        PASS();
    } else {
        FAIL("uthread_self returned wrong handle");
    }
}

void test_exit(void)
{
    TEST("uthread_exit");
    uthread_t thread;
    void *retval;
    int marker = 0;

    int ret = uthread_create(&thread, NULL, exit_thread, &marker);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread, &retval);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if ((intptr_t)retval == 42 && marker == 0) {
        PASS();
    } else {
        FAIL("uthread_exit did not work correctly");
    }
}

void test_detached(void)
{
    TEST("Detached thread");
    int counter = 0;

    uthread_attr_t attr;
    uthread_attr_init(&attr);
    uthread_attr_setdetachstate(&attr, UTHREAD_CREATE_DETACHED);

    uthread_t thread;
    int ret = uthread_create(&thread, &attr, simple_thread, &counter);
    uthread_attr_destroy(&attr);

    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    /* Cannot join detached thread */
    ret = uthread_join(thread, NULL);
    if (ret == 0) {
        FAIL("Should not be able to join detached thread");
        return;
    }

    /* Wait a bit for detached thread to complete */
    uthread_sleep(50);

    if (counter == 1) {
        PASS();
    } else {
        FAIL("Detached thread did not execute");
    }
}

void test_sleep(void)
{
    TEST("uthread_sleep");
    int counter = 0;
    uthread_t thread;

    int ret = uthread_create(&thread, NULL, sleep_thread, &counter);
    if (ret != 0) {
        FAIL("uthread_create failed");
        return;
    }

    ret = uthread_join(thread, NULL);
    if (ret != 0) {
        FAIL("uthread_join failed");
        return;
    }

    if (counter == 1) {
        PASS();
    } else {
        FAIL("Thread did not complete after sleep");
    }
}

void test_attributes(void)
{
    TEST("Thread attributes");
    uthread_attr_t attr;
    size_t stack_size;
    int priority;
    int detach_state;

    int ret = uthread_attr_init(&attr);
    if (ret != 0) {
        FAIL("uthread_attr_init failed");
        return;
    }

    ret = uthread_attr_setstacksize(&attr, 32 * 1024);
    ret |= uthread_attr_setpriority(&attr, 20);
    ret |= uthread_attr_setdetachstate(&attr, UTHREAD_CREATE_JOINABLE);
    if (ret != 0) {
        FAIL("Setting attributes failed");
        return;
    }

    ret = uthread_attr_getstacksize(&attr, &stack_size);
    ret |= uthread_attr_getpriority(&attr, &priority);
    ret |= uthread_attr_getdetachstate(&attr, &detach_state);
    if (ret != 0) {
        FAIL("Getting attributes failed");
        return;
    }

    if (stack_size == 32 * 1024 && priority == 20 &&
        detach_state == UTHREAD_CREATE_JOINABLE) {
        PASS();
    } else {
        FAIL("Attribute values incorrect");
    }

    uthread_attr_destroy(&attr);
}

void test_thread_name(void)
{
    TEST("Thread naming");
    uthread_t self = uthread_self();
    char name[UTHREAD_NAME_MAX];

    int ret = uthread_setname(self, "TestThread");
    if (ret != 0) {
        FAIL("uthread_setname failed");
        return;
    }

    ret = uthread_getname(self, name, sizeof(name));
    if (ret != 0) {
        FAIL("uthread_getname failed");
        return;
    }

    if (strcmp(name, "TestThread") == 0) {
        PASS();
    } else {
        FAIL("Thread name mismatch");
    }
}

void test_shutdown(void)
{
    TEST("uthread_shutdown");
    uthread_shutdown();
    if (!uthread_is_initialized()) {
        PASS();
    } else {
        FAIL("Library still initialized after shutdown");
    }
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== LibUThread Basic Tests ===\n\n");

    test_init();
    test_create_single();
    test_create_many();
    test_return_value();
    test_yield();
    test_self();
    test_exit();
    test_detached();
    test_sleep();
    test_attributes();
    test_thread_name();
    test_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
