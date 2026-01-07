/**
 * Mutex Benchmark
 *
 * Measures mutex lock/unlock performance for LibUThread.
 *
 * @file mutex.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define NUM_OPERATIONS 100000
#define NUM_ITERATIONS 5
#define NUM_THREADS 4

static uthread_mutex_t g_mutex;
static int g_counter;

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ==========================================================================
 * Uncontended Mutex Benchmark
 * ========================================================================== */

static void benchmark_uncontended(void)
{
    printf("\n--- Uncontended Mutex ---\n");

    double total_ns = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        uthread_mutex_init(&g_mutex, NULL);

        uint64_t start = get_time_ns();
        for (int i = 0; i < NUM_OPERATIONS; i++) {
            uthread_mutex_lock(&g_mutex);
            g_counter++;
            uthread_mutex_unlock(&g_mutex);
        }
        uint64_t end = get_time_ns();

        uthread_mutex_destroy(&g_mutex);

        double elapsed_ns = (double)(end - start);
        double per_op_ns = elapsed_ns / NUM_OPERATIONS;
        total_ns += per_op_ns;

        printf("Iteration %d: %.2f ns/lock-unlock\n", iter + 1, per_op_ns);
    }

    printf("Average: %.2f ns/lock-unlock\n", total_ns / NUM_ITERATIONS);
    printf("Rate: %.0f operations/sec\n", 1e9 / (total_ns / NUM_ITERATIONS));
}

/* ==========================================================================
 * Contended Mutex Benchmark
 * ========================================================================== */

typedef struct {
    int operations;
} worker_args_t;

static void *contended_worker(void *arg)
{
    worker_args_t *args = (worker_args_t *)arg;

    for (int i = 0; i < args->operations; i++) {
        uthread_mutex_lock(&g_mutex);
        g_counter++;
        uthread_mutex_unlock(&g_mutex);
    }

    return NULL;
}

static void benchmark_contended(sched_policy_t policy, const char *name)
{
    printf("\n--- Contended Mutex (%s) ---\n", name);

    double total_ns = 0;
    int ops_per_thread = NUM_OPERATIONS / NUM_THREADS;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        if (uthread_init(policy) != 0) {
            fprintf(stderr, "Failed to initialize\n");
            return;
        }

        uthread_mutex_init(&g_mutex, NULL);
        g_counter = 0;

        worker_args_t args = { .operations = ops_per_thread };
        uthread_t threads[NUM_THREADS];

        uint64_t start = get_time_ns();

        for (int i = 0; i < NUM_THREADS; i++) {
            uthread_create(&threads[i], NULL, contended_worker, &args);
        }

        for (int i = 0; i < NUM_THREADS; i++) {
            uthread_join(threads[i], NULL);
        }

        uint64_t end = get_time_ns();

        uthread_mutex_destroy(&g_mutex);
        uthread_shutdown();

        double elapsed_ns = (double)(end - start);
        double per_op_ns = elapsed_ns / (ops_per_thread * NUM_THREADS);
        total_ns += per_op_ns;

        printf("Iteration %d: %.2f ns/lock-unlock (counter=%d)\n",
               iter + 1, per_op_ns, g_counter);
    }

    printf("Average: %.2f ns/lock-unlock\n", total_ns / NUM_ITERATIONS);
    printf("Rate: %.0f operations/sec\n", 1e9 / (total_ns / NUM_ITERATIONS));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Mutex Benchmark ===\n");
    printf("Operations: %d, Iterations: %d\n", NUM_OPERATIONS, NUM_ITERATIONS);

    /* Need library initialized for uncontended test */
    if (uthread_init(SCHED_ROUND_ROBIN) != 0) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }
    uthread_set_preemption(false);

    benchmark_uncontended();

    uthread_shutdown();

    /* Contended benchmarks */
    printf("\nContention tests with %d threads:\n", NUM_THREADS);
    benchmark_contended(SCHED_ROUND_ROBIN, "Round-Robin");
    benchmark_contended(SCHED_PRIORITY, "Priority");
    benchmark_contended(SCHED_CFS, "CFS");

    printf("\n=== Benchmark Complete ===\n");

    return 0;
}
