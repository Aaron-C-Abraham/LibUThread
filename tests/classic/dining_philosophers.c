/**
 * Dining Philosophers Problem
 *
 * Classic concurrency problem implementation using LibUThread.
 * Uses resource hierarchy solution to prevent deadlock.
 *
 * @file dining_philosophers.c
 */

#include <stdio.h>
#include <stdlib.h>
#include "uthread.h"

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define NUM_PHILOSOPHERS 5
#define MEALS_PER_PHILOSOPHER 5

static uthread_mutex_t forks[NUM_PHILOSOPHERS];
static int meals_eaten[NUM_PHILOSOPHERS];

/* ==========================================================================
 * Philosopher Thread
 *
 * Uses resource hierarchy: always pick up lower-numbered fork first.
 * This prevents deadlock by avoiding circular wait.
 * ========================================================================== */

static void think(int id)
{
    printf("[Philosopher %d] Thinking...\n", id);
    /* Simulate thinking */
    for (volatile int i = 0; i < 1000; i++);
    uthread_yield();
}

static void eat(int id)
{
    printf("[Philosopher %d] Eating meal %d...\n", id, meals_eaten[id] + 1);
    meals_eaten[id]++;
    /* Simulate eating */
    for (volatile int i = 0; i < 1000; i++);
    uthread_yield();
}

static void *philosopher(void *arg)
{
    int id = (int)(intptr_t)arg;
    int left_fork = id;
    int right_fork = (id + 1) % NUM_PHILOSOPHERS;

    /* Resource hierarchy: always pick up lower-numbered fork first */
    int first_fork = (left_fork < right_fork) ? left_fork : right_fork;
    int second_fork = (left_fork < right_fork) ? right_fork : left_fork;

    for (int meal = 0; meal < MEALS_PER_PHILOSOPHER; meal++) {
        /* Think */
        think(id);

        /* Pick up forks (in order to prevent deadlock) */
        printf("[Philosopher %d] Picking up fork %d\n", id, first_fork);
        uthread_mutex_lock(&forks[first_fork]);

        printf("[Philosopher %d] Picking up fork %d\n", id, second_fork);
        uthread_mutex_lock(&forks[second_fork]);

        /* Eat */
        eat(id);

        /* Put down forks */
        printf("[Philosopher %d] Putting down fork %d\n", id, second_fork);
        uthread_mutex_unlock(&forks[second_fork]);

        printf("[Philosopher %d] Putting down fork %d\n", id, first_fork);
        uthread_mutex_unlock(&forks[first_fork]);
    }

    printf("[Philosopher %d] Done eating all meals!\n", id);

    return NULL;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("=== Dining Philosophers Problem ===\n");
    printf("Philosophers: %d, Meals each: %d\n\n", NUM_PHILOSOPHERS, MEALS_PER_PHILOSOPHER);

    /* Initialize library */
    if (uthread_init(SCHED_ROUND_ROBIN) != 0) {
        fprintf(stderr, "Failed to initialize threading library\n");
        return 1;
    }

    /* Initialize forks (mutexes) */
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        uthread_mutex_init(&forks[i], NULL);
        meals_eaten[i] = 0;
    }

    /* Create philosopher threads */
    uthread_t philosophers[NUM_PHILOSOPHERS];
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        uthread_create(&philosophers[i], NULL, philosopher, (void *)(intptr_t)i);
    }

    /* Wait for all philosophers */
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        uthread_join(philosophers[i], NULL);
    }

    /* Cleanup */
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        uthread_mutex_destroy(&forks[i]);
    }

    uthread_shutdown();

    /* Verify results */
    printf("\n=== Results ===\n");
    int success = 1;
    int total_meals = 0;
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        printf("Philosopher %d ate %d meals\n", i, meals_eaten[i]);
        total_meals += meals_eaten[i];
        if (meals_eaten[i] != MEALS_PER_PHILOSOPHER) {
            success = 0;
        }
    }

    printf("\nTotal meals: %d (expected %d)\n",
           total_meals, NUM_PHILOSOPHERS * MEALS_PER_PHILOSOPHER);

    if (success) {
        printf("SUCCESS: All philosophers ate all their meals!\n");
        return 0;
    } else {
        printf("FAILURE: Some philosophers didn't finish\n");
        return 1;
    }
}
