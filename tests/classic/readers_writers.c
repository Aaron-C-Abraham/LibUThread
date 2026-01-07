/**
 * Readers-Writers Problem
 *
 * Classic concurrency problem implementation using LibUThread.
 * Uses rwlock for synchronized access to shared data.
 *
 * @file readers_writers.c
 */

#include <stdio.h>
#include <stdlib.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define NUM_READERS 5
#define NUM_WRITERS 2
#define READS_PER_READER 10
#define WRITES_PER_WRITER 5

/* Shared data */
static int shared_data = 0;
static uthread_rwlock_t rwlock;

/* Statistics */
static int total_reads = 0;
static int total_writes = 0;
static uthread_mutex_t stats_mutex;

/* ==========================================================================
 * Reader Thread
 * ========================================================================== */

static void *reader(void *arg)
{
    int id = (int)(intptr_t)arg;
    int local_reads = 0;

    for (int i = 0; i < READS_PER_READER; i++) {
        /* Acquire read lock */
        uthread_rwlock_rdlock(&rwlock);

        /* Read shared data */
        int value = shared_data;
        printf("[Reader %d] Read value: %d\n", id, value);
        local_reads++;

        /* Hold lock briefly */
        uthread_yield();

        /* Release read lock */
        uthread_rwlock_unlock(&rwlock);

        /* Do some processing */
        uthread_yield();
    }

    /* Update statistics */
    uthread_mutex_lock(&stats_mutex);
    total_reads += local_reads;
    uthread_mutex_unlock(&stats_mutex);

    printf("[Reader %d] Finished (%d reads)\n", id, local_reads);

    return NULL;
}

/* ==========================================================================
 * Writer Thread
 * ========================================================================== */

static void *writer(void *arg)
{
    int id = (int)(intptr_t)arg;
    int local_writes = 0;

    for (int i = 0; i < WRITES_PER_WRITER; i++) {
        /* Acquire write lock */
        uthread_rwlock_wrlock(&rwlock);

        /* Modify shared data */
        shared_data = id * 100 + i;
        printf("[Writer %d] Wrote value: %d\n", id, shared_data);
        local_writes++;

        /* Hold lock briefly */
        uthread_yield();

        /* Release write lock */
        uthread_rwlock_unlock(&rwlock);

        /* Do some processing */
        uthread_yield();
    }

    /* Update statistics */
    uthread_mutex_lock(&stats_mutex);
    total_writes += local_writes;
    uthread_mutex_unlock(&stats_mutex);

    printf("[Writer %d] Finished (%d writes)\n", id, local_writes);

    return NULL;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Readers-Writers Problem ===\n");
    printf("Readers: %d (%d reads each), Writers: %d (%d writes each)\n\n",
           NUM_READERS, READS_PER_READER,
           NUM_WRITERS, WRITES_PER_WRITER);

    /* Initialize library */
    if (uthread_init(SCHED_ROUND_ROBIN) != 0) {
        fprintf(stderr, "Failed to initialize threading library\n");
        return 1;
    }

    /* Initialize synchronization */
    uthread_rwlock_init(&rwlock, NULL);
    uthread_mutex_init(&stats_mutex, NULL);

    /* Create threads */
    uthread_t readers[NUM_READERS];
    uthread_t writers[NUM_WRITERS];

    /* Start writers first */
    for (int i = 0; i < NUM_WRITERS; i++) {
        uthread_create(&writers[i], NULL, writer, (void *)(intptr_t)i);
    }

    /* Start readers */
    for (int i = 0; i < NUM_READERS; i++) {
        uthread_create(&readers[i], NULL, reader, (void *)(intptr_t)i);
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_WRITERS; i++) {
        uthread_join(writers[i], NULL);
    }

    for (int i = 0; i < NUM_READERS; i++) {
        uthread_join(readers[i], NULL);
    }

    /* Cleanup */
    uthread_rwlock_destroy(&rwlock);
    uthread_mutex_destroy(&stats_mutex);

    uthread_shutdown();

    /* Verify results */
    printf("\n=== Results ===\n");
    printf("Total reads: %d (expected %d)\n",
           total_reads, NUM_READERS * READS_PER_READER);
    printf("Total writes: %d (expected %d)\n",
           total_writes, NUM_WRITERS * WRITES_PER_WRITER);
    printf("Final shared_data value: %d\n", shared_data);

    int expected_reads = NUM_READERS * READS_PER_READER;
    int expected_writes = NUM_WRITERS * WRITES_PER_WRITER;

    if (total_reads == expected_reads && total_writes == expected_writes) {
        printf("SUCCESS: All reads and writes completed correctly!\n");
        return 0;
    } else {
        printf("FAILURE: Mismatch in operation counts\n");
        return 1;
    }
}
