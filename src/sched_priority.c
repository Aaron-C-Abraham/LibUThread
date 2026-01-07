/**
 * LibUThread Priority Scheduler
 *
 * Multi-level priority queue scheduler with 32 priority levels.
 * Higher priority threads always run before lower priority ones.
 * Within the same priority level, threads run in round-robin fashion.
 *
 * @file sched_priority.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Global Priority scheduler state */
struct sched_priority_state g_priority_state = {0};

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Find the highest priority non-empty queue.
 * Uses the bitmap for O(1) lookup.
 *
 * @return Highest priority level with threads, or -1 if all empty
 */
static int find_highest_priority(void)
{
    if (g_priority_state.bitmap == 0) {
        return -1;
    }

    /* Find the position of the highest set bit */
    /* Priority 31 is highest, stored in bit 31 */
    for (int i = UTHREAD_PRIORITY_LEVELS - 1; i >= 0; i--) {
        if (g_priority_state.bitmap & (1U << i)) {
            return i;
        }
    }

    return -1;
}

/**
 * Add thread to a specific priority queue.
 *
 * @param thread Thread to add
 * @param priority Priority level (0-31)
 */
static void add_to_priority_queue(struct uthread_internal *thread, int priority)
{
    thread->next = NULL;

    /* Find the tail of this priority queue */
    struct uthread_internal *tail = g_priority_state.queues[priority];
    if (tail == NULL) {
        /* Queue was empty */
        g_priority_state.queues[priority] = thread;
        thread->prev = NULL;
        /* Set bitmap bit */
        g_priority_state.bitmap |= (1U << priority);
    } else {
        /* Find actual tail */
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = thread;
        thread->prev = tail;
    }
}

/**
 * Remove thread from a specific priority queue.
 *
 * @param thread Thread to remove
 * @param priority Priority level (0-31)
 */
static void remove_from_priority_queue(struct uthread_internal *thread, int priority)
{
    /* Update links */
    if (thread->prev != NULL) {
        thread->prev->next = thread->next;
    } else {
        /* Was head of queue */
        g_priority_state.queues[priority] = thread->next;
    }

    if (thread->next != NULL) {
        thread->next->prev = thread->prev;
    }

    thread->next = NULL;
    thread->prev = NULL;

    /* Clear bitmap bit if queue is now empty */
    if (g_priority_state.queues[priority] == NULL) {
        g_priority_state.bitmap &= ~(1U << priority);
    }
}

/* ==========================================================================
 * Priority Scheduler Implementation
 * ========================================================================== */

static int priority_init(void)
{
    memset(&g_priority_state, 0, sizeof(g_priority_state));
    return 0;
}

static void priority_shutdown(void)
{
    memset(&g_priority_state, 0, sizeof(g_priority_state));
}

static void priority_enqueue(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    int priority = thread->priority;
    if (priority < 0) priority = 0;
    if (priority >= UTHREAD_PRIORITY_LEVELS) priority = UTHREAD_PRIORITY_LEVELS - 1;

    add_to_priority_queue(thread, priority);
    g_priority_state.count++;

    /* Reset timeslice */
    thread->timeslice_remaining = g_scheduler.timeslice_ns;
}

static struct uthread_internal *priority_dequeue(void)
{
    int highest = find_highest_priority();
    if (highest < 0) {
        return NULL;
    }

    /* Get head of highest priority queue */
    struct uthread_internal *thread = g_priority_state.queues[highest];
    if (thread != NULL) {
        remove_from_priority_queue(thread, highest);
        g_priority_state.count--;
    }

    return thread;
}

static void priority_remove(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    int priority = thread->priority;
    if (priority < 0) priority = 0;
    if (priority >= UTHREAD_PRIORITY_LEVELS) priority = UTHREAD_PRIORITY_LEVELS - 1;

    /* Check if in this queue */
    struct uthread_internal *t = g_priority_state.queues[priority];
    while (t != NULL) {
        if (t == thread) {
            remove_from_priority_queue(thread, priority);
            g_priority_state.count--;
            return;
        }
        t = t->next;
    }

    /* Thread might have been moved to different priority - search all */
    for (int i = 0; i < UTHREAD_PRIORITY_LEVELS; i++) {
        t = g_priority_state.queues[i];
        while (t != NULL) {
            if (t == thread) {
                remove_from_priority_queue(thread, i);
                g_priority_state.count--;
                return;
            }
            t = t->next;
        }
    }
}

static void priority_on_yield(struct uthread_internal *thread)
{
    (void)thread;
    /* Thread goes to back of its priority queue */
}

static void priority_on_tick(struct uthread_internal *thread, uint64_t elapsed_ns)
{
    if (thread == NULL) return;

    /* Decrease remaining timeslice */
    if (thread->timeslice_remaining > elapsed_ns) {
        thread->timeslice_remaining -= elapsed_ns;
    } else {
        thread->timeslice_remaining = 0;
    }
}

static bool priority_should_preempt(struct uthread_internal *current)
{
    if (current == NULL) return false;

    int highest = find_highest_priority();

    /* Preempt if:
     * 1. A higher priority thread is ready, OR
     * 2. Timeslice exhausted and same-priority threads waiting
     */
    if (highest > current->priority) {
        return true;
    }

    if (current->timeslice_remaining == 0 &&
        g_priority_state.queues[current->priority] != NULL) {
        return true;
    }

    return false;
}

static void priority_update_priority(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* If thread is in a queue, move it to the new priority queue */
    /* First, try to remove from current location */
    for (int i = 0; i < UTHREAD_PRIORITY_LEVELS; i++) {
        struct uthread_internal *t = g_priority_state.queues[i];
        while (t != NULL) {
            if (t == thread) {
                /* Found it - remove and re-add at new priority */
                remove_from_priority_queue(thread, i);
                add_to_priority_queue(thread, thread->priority);
                return;
            }
            t = t->next;
        }
    }

    /* Thread wasn't in any queue (probably running) - nothing to do */
}

static const char *priority_name(void)
{
    return "Priority";
}

/* Scheduler operations structure */
struct scheduler_ops sched_priority_ops = {
    .init = priority_init,
    .shutdown = priority_shutdown,
    .enqueue = priority_enqueue,
    .dequeue = priority_dequeue,
    .remove = priority_remove,
    .on_yield = priority_on_yield,
    .on_tick = priority_on_tick,
    .should_preempt = priority_should_preempt,
    .update_priority = priority_update_priority,
    .name = priority_name
};
