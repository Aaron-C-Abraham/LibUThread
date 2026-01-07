/**
 * Thread Creation Benchmark
 *
 * Measures thread creation and join latency for LibUThread.
 *
 * @file creation.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define NUM_THREADS 1000
#define NUM_ITERATIONS 5

/* ==========================================================================
 * Minimal Thread Function
 * ========================================================================== */

static void *empty_thread(void *arg)
{
    (void)arg;
    return NULL;
}

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void run_benchmark(sched_policy_t policy, const char *name)
{
    printf("\n--- %s Scheduler ---\n", name);

    double total_create_ns = 0;
    double total_join_ns = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        if (uthread_init(policy) != 0) {
            fprintf(stderr, "Failed to initialize\n");
            return;
        }

        /* Disable preemption for accurate measurement */
        uthread_set_preemption(false);

        uthread_t threads[NUM_THREADS];

        /* Benchmark creation */
        uint64_t create_start = get_time_ns();
        for (int i = 0; i < NUM_THREADS; i++) {
            uthread_create(&threads[i], NULL, empty_thread, NULL);
        }
        uint64_t create_end = get_time_ns();

        /* Benchmark joining */
        uint64_t join_start = get_time_ns();
        for (int i = 0; i < NUM_THREADS; i++) {
            uthread_join(threads[i], NULL);
        }
        uint64_t join_end = get_time_ns();

        uthread_shutdown();

        double create_ns = (double)(create_end - create_start) / NUM_THREADS;
        double join_ns = (double)(join_end - join_start) / NUM_THREADS;

        total_create_ns += create_ns;
        total_join_ns += join_ns;

        printf("Iteration %d: create=%.2f ns, join=%.2f ns\n",
               iter + 1, create_ns, join_ns);
    }

    double avg_create = total_create_ns / NUM_ITERATIONS;
    double avg_join = total_join_ns / NUM_ITERATIONS;

    printf("Average: create=%.2f ns/thread, join=%.2f ns/thread\n",
           avg_create, avg_join);
    printf("Rate: %.0f creates/sec, %.0f joins/sec\n",
           1e9 / avg_create, 1e9 / avg_join);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Thread Creation Benchmark ===\n");
    printf("Threads: %d, Iterations: %d\n", NUM_THREADS, NUM_ITERATIONS);

    run_benchmark(SCHED_ROUND_ROBIN, "Round-Robin");
    run_benchmark(SCHED_PRIORITY, "Priority");
    run_benchmark(SCHED_CFS, "CFS");

    printf("\n=== Benchmark Complete ===\n");

    return 0;
}
