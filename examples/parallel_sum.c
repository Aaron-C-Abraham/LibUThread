/**
 * Parallel Sum Example
 *
 * Demonstrates parallel computation using LibUThread.
 * Computes the sum of an array using multiple threads.
 *
 * @file parallel_sum.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define ARRAY_SIZE 10000
#define NUM_THREADS 4

static int array[ARRAY_SIZE];
static long partial_sums[NUM_THREADS];

/* ==========================================================================
 * Thread Arguments
 * ========================================================================== */

typedef struct {
    int thread_id;
    int start_index;
    int end_index;
} work_args_t;

/* ==========================================================================
 * Worker Thread
 * ========================================================================== */

static void *sum_worker(void *arg)
{
    work_args_t *args = (work_args_t *)arg;
    long sum = 0;

    printf("[Thread %d] Computing sum for indices %d to %d\n",
           args->thread_id, args->start_index, args->end_index - 1);

    for (int i = args->start_index; i < args->end_index; i++) {
        sum += array[i];
    }

    partial_sums[args->thread_id] = sum;

    printf("[Thread %d] Partial sum: %ld\n", args->thread_id, sum);

    return (void *)sum;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Parallel Sum Example ===\n\n");

    /* Initialize array with values 1 to ARRAY_SIZE */
    printf("Initializing array with %d elements...\n", ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i + 1;
    }

    /* Expected sum: n*(n+1)/2 */
    long expected_sum = (long)ARRAY_SIZE * (ARRAY_SIZE + 1) / 2;
    printf("Expected sum: %ld\n\n", expected_sum);

    /* Initialize threading library */
    if (uthread_init(SCHED_ROUND_ROBIN) != 0) {
        fprintf(stderr, "Failed to initialize threading library\n");
        return 1;
    }

    /* Calculate work distribution */
    int chunk_size = ARRAY_SIZE / NUM_THREADS;
    int remainder = ARRAY_SIZE % NUM_THREADS;

    /* Create worker threads */
    uthread_t threads[NUM_THREADS];
    work_args_t args[NUM_THREADS];

    int current_index = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].start_index = current_index;

        /* Distribute remainder among first threads */
        int this_chunk = chunk_size + (i < remainder ? 1 : 0);
        args[i].end_index = current_index + this_chunk;

        current_index = args[i].end_index;

        uthread_create(&threads[i], NULL, sum_worker, &args[i]);
    }

    /* Wait for all threads and collect results */
    long total_sum = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        void *retval;
        uthread_join(threads[i], &retval);
        total_sum += (long)retval;
    }

    uthread_shutdown();

    /* Verify result */
    printf("\n=== Results ===\n");
    printf("Total sum (from thread returns): %ld\n", total_sum);

    long sum_from_partials = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        sum_from_partials += partial_sums[i];
    }
    printf("Total sum (from partial_sums):   %ld\n", sum_from_partials);
    printf("Expected sum:                    %ld\n", expected_sum);

    if (total_sum == expected_sum && sum_from_partials == expected_sum) {
        printf("\nSUCCESS: Parallel sum computed correctly!\n");
        return 0;
    } else {
        printf("\nFAILURE: Sum mismatch\n");
        return 1;
    }
}
