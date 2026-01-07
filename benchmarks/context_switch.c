/**
 * Context Switch Benchmark
 *
 * Measures context switch latency for LibUThread.
 *
 * @file context_switch.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define NUM_SWITCHES 10000
#define NUM_ITERATIONS 5

static uthread_t thread_a;
static uthread_t thread_b;
static volatile int turn = 0;
static int switches_done = 0;

/* ==========================================================================
 * Ping-Pong Threads
 * ========================================================================== */

static void *ping_thread(void *arg)
{
    (void)arg;

    while (switches_done < NUM_SWITCHES) {
        while (turn != 0 && switches_done < NUM_SWITCHES) {
            uthread_yield();
        }
        if (switches_done >= NUM_SWITCHES) break;

        switches_done++;
        turn = 1;
        uthread_yield();
    }

    return NULL;
}

static void *pong_thread(void *arg)
{
    (void)arg;

    while (switches_done < NUM_SWITCHES) {
        while (turn != 1 && switches_done < NUM_SWITCHES) {
            uthread_yield();
        }
        if (switches_done >= NUM_SWITCHES) break;

        switches_done++;
        turn = 0;
        uthread_yield();
    }

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

    double total_ns = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        if (uthread_init(policy) != 0) {
            fprintf(stderr, "Failed to initialize\n");
            return;
        }

        /* Disable preemption for accurate measurement */
        uthread_set_preemption(false);

        turn = 0;
        switches_done = 0;

        uthread_create(&thread_a, NULL, ping_thread, NULL);
        uthread_create(&thread_b, NULL, pong_thread, NULL);

        uint64_t start = get_time_ns();

        uthread_join(thread_a, NULL);
        uthread_join(thread_b, NULL);

        uint64_t end = get_time_ns();

        uthread_shutdown();

        double elapsed_ns = (double)(end - start);
        double per_switch_ns = elapsed_ns / NUM_SWITCHES;
        total_ns += per_switch_ns;

        printf("Iteration %d: %.2f ns/switch (%.2f us total)\n",
               iter + 1, per_switch_ns, elapsed_ns / 1000.0);
    }

    double avg_ns = total_ns / NUM_ITERATIONS;
    printf("Average: %.2f ns/switch\n", avg_ns);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Context Switch Benchmark ===\n");
    printf("Switches: %d, Iterations: %d\n", NUM_SWITCHES, NUM_ITERATIONS);

    run_benchmark(SCHED_ROUND_ROBIN, "Round-Robin");
    run_benchmark(SCHED_PRIORITY, "Priority");
    run_benchmark(SCHED_CFS, "CFS");

    printf("\n=== Benchmark Complete ===\n");

    return 0;
}
