/**
 * Producer-Consumer Problem
 *
 * Classic concurrency problem implementation using LibUThread.
 *
 * @file producer_consumer.c
 */

#include <stdio.h>
#include <stdlib.h>
#include "uthread.h"

/* ==========================================================================
 * Bounded Buffer
 * ========================================================================== */

#define BUFFER_SIZE 10
#define ITEMS_PER_PRODUCER 50
#define NUM_PRODUCERS 3
#define NUM_CONSUMERS 2

static int buffer[BUFFER_SIZE];
static int buffer_count = 0;
static int buffer_in = 0;
static int buffer_out = 0;

static uthread_mutex_t buffer_mutex;
static uthread_cond_t buffer_not_full;
static uthread_cond_t buffer_not_empty;

static int items_produced = 0;
static int items_consumed = 0;
static int done_producing = 0;

/* ==========================================================================
 * Producer Thread
 * ========================================================================== */

static void *producer(void *arg)
{
    int id = (int)(intptr_t)arg;

    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 1000 + i;  /* Unique item value */

        uthread_mutex_lock(&buffer_mutex);

        /* Wait while buffer is full */
        while (buffer_count >= BUFFER_SIZE) {
            uthread_cond_wait(&buffer_not_full, &buffer_mutex);
        }

        /* Add item to buffer */
        buffer[buffer_in] = item;
        buffer_in = (buffer_in + 1) % BUFFER_SIZE;
        buffer_count++;
        items_produced++;

        printf("[Producer %d] Produced item %d (buffer: %d/%d)\n",
               id, item, buffer_count, BUFFER_SIZE);

        /* Signal consumer */
        uthread_cond_signal(&buffer_not_empty);

        uthread_mutex_unlock(&buffer_mutex);

        /* Simulate work */
        uthread_yield();
    }

    printf("[Producer %d] Finished\n", id);

    /* Signal that this producer is done */
    uthread_mutex_lock(&buffer_mutex);
    done_producing++;
    uthread_cond_broadcast(&buffer_not_empty);  /* Wake consumers to check done */
    uthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

/* ==========================================================================
 * Consumer Thread
 * ========================================================================== */

static void *consumer(void *arg)
{
    int id = (int)(intptr_t)arg;
    int consumed = 0;

    while (1) {
        uthread_mutex_lock(&buffer_mutex);

        /* Wait while buffer is empty (but producers not done) */
        while (buffer_count == 0 && done_producing < NUM_PRODUCERS) {
            uthread_cond_wait(&buffer_not_empty, &buffer_mutex);
        }

        /* Check if done */
        if (buffer_count == 0 && done_producing >= NUM_PRODUCERS) {
            uthread_mutex_unlock(&buffer_mutex);
            break;
        }

        /* Remove item from buffer */
        int item = buffer[buffer_out];
        buffer_out = (buffer_out + 1) % BUFFER_SIZE;
        buffer_count--;
        items_consumed++;
        consumed++;

        printf("[Consumer %d] Consumed item %d (buffer: %d/%d)\n",
               id, item, buffer_count, BUFFER_SIZE);

        /* Signal producer */
        uthread_cond_signal(&buffer_not_full);

        uthread_mutex_unlock(&buffer_mutex);

        /* Simulate processing */
        uthread_yield();
    }

    printf("[Consumer %d] Finished (consumed %d items)\n", id, consumed);

    return NULL;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Producer-Consumer Problem ===\n");
    printf("Producers: %d, Consumers: %d, Buffer size: %d\n",
           NUM_PRODUCERS, NUM_CONSUMERS, BUFFER_SIZE);
    printf("Items per producer: %d, Total items: %d\n\n",
           ITEMS_PER_PRODUCER, NUM_PRODUCERS * ITEMS_PER_PRODUCER);

    /* Initialize library */
    if (uthread_init(SCHED_ROUND_ROBIN) != 0) {
        fprintf(stderr, "Failed to initialize threading library\n");
        return 1;
    }

    /* Initialize synchronization */
    uthread_mutex_init(&buffer_mutex, NULL);
    uthread_cond_init(&buffer_not_full, NULL);
    uthread_cond_init(&buffer_not_empty, NULL);

    /* Create threads */
    uthread_t producers[NUM_PRODUCERS];
    uthread_t consumers[NUM_CONSUMERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        uthread_create(&producers[i], NULL, producer, (void *)(intptr_t)i);
    }

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        uthread_create(&consumers[i], NULL, consumer, (void *)(intptr_t)i);
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        uthread_join(producers[i], NULL);
    }

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        uthread_join(consumers[i], NULL);
    }

    /* Cleanup */
    uthread_cond_destroy(&buffer_not_full);
    uthread_cond_destroy(&buffer_not_empty);
    uthread_mutex_destroy(&buffer_mutex);

    uthread_shutdown();

    /* Verify */
    printf("\n=== Results ===\n");
    printf("Items produced: %d\n", items_produced);
    printf("Items consumed: %d\n", items_consumed);

    int expected = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    if (items_produced == expected && items_consumed == expected) {
        printf("SUCCESS: All items produced and consumed correctly!\n");
        return 0;
    } else {
        printf("FAILURE: Mismatch in item counts\n");
        return 1;
    }
}
