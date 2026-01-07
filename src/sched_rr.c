/**
 * LibUThread Round-Robin Scheduler
 *
 * Simple FIFO queue scheduler with time-sliced execution.
 *
 * @file sched_rr.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Global Round-Robin state */
struct sched_rr_state g_rr_state = {0};

/* ==========================================================================
 * Round-Robin Scheduler Implementation
 * ========================================================================== */

static int rr_init(void)
{
    memset(&g_rr_state, 0, sizeof(g_rr_state));
    return 0;
}

static void rr_shutdown(void)
{
    /* Nothing to free - threads are managed by main scheduler */
    memset(&g_rr_state, 0, sizeof(g_rr_state));
}

static void rr_enqueue(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Add to tail of queue (FIFO) */
    thread->next = NULL;
    thread->prev = g_rr_state.tail;

    if (g_rr_state.tail != NULL) {
        g_rr_state.tail->next = thread;
    } else {
        g_rr_state.head = thread;
    }

    g_rr_state.tail = thread;
    g_rr_state.count++;

    /* Reset timeslice for this thread */
    thread->timeslice_remaining = g_scheduler.timeslice_ns;
}

static struct uthread_internal *rr_dequeue(void)
{
    if (g_rr_state.head == NULL) {
        return NULL;
    }

    /* Remove from head of queue (FIFO) */
    struct uthread_internal *thread = g_rr_state.head;

    g_rr_state.head = thread->next;
    if (g_rr_state.head != NULL) {
        g_rr_state.head->prev = NULL;
    } else {
        g_rr_state.tail = NULL;
    }

    thread->next = NULL;
    thread->prev = NULL;
    g_rr_state.count--;

    return thread;
}

static void rr_remove(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Check if in queue */
    bool found = false;
    struct uthread_internal *t = g_rr_state.head;
    while (t != NULL) {
        if (t == thread) {
            found = true;
            break;
        }
        t = t->next;
    }

    if (!found) return;

    /* Unlink from queue */
    if (thread->prev != NULL) {
        thread->prev->next = thread->next;
    } else {
        g_rr_state.head = thread->next;
    }

    if (thread->next != NULL) {
        thread->next->prev = thread->prev;
    } else {
        g_rr_state.tail = thread->prev;
    }

    thread->next = NULL;
    thread->prev = NULL;
    g_rr_state.count--;
}

static void rr_on_yield(struct uthread_internal *thread)
{
    (void)thread;
    /* Nothing special for yield in RR - thread goes to back of queue */
}

static void rr_on_tick(struct uthread_internal *thread, uint64_t elapsed_ns)
{
    if (thread == NULL) return;

    /* Decrease remaining timeslice */
    if (thread->timeslice_remaining > elapsed_ns) {
        thread->timeslice_remaining -= elapsed_ns;
    } else {
        thread->timeslice_remaining = 0;
    }
}

static bool rr_should_preempt(struct uthread_internal *current)
{
    if (current == NULL) return false;

    /* Preempt if timeslice exhausted and other threads waiting */
    return (current->timeslice_remaining == 0 && g_rr_state.count > 0);
}

static void rr_update_priority(struct uthread_internal *thread)
{
    (void)thread;
    /* Round-robin ignores priority */
}

static const char *rr_name(void)
{
    return "Round-Robin";
}

/* Scheduler operations structure */
struct scheduler_ops sched_rr_ops = {
    .init = rr_init,
    .shutdown = rr_shutdown,
    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .remove = rr_remove,
    .on_yield = rr_on_yield,
    .on_tick = rr_on_tick,
    .should_preempt = rr_should_preempt,
    .update_priority = rr_update_priority,
    .name = rr_name
};
