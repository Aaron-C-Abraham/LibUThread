/**
 * LibUThread CFS (Completely Fair Scheduler)
 *
 * Simplified implementation of Linux's CFS scheduler.
 * Uses a red-black tree sorted by virtual runtime (vruntime).
 * Threads with lower vruntime get priority.
 *
 * @file sched_cfs.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Global CFS state */
struct sched_cfs_state g_cfs_state = {0};

/* ==========================================================================
 * Red-Black Tree Operations
 *
 * Simplified RB-tree implementation for CFS.
 * Key: vruntime (lower = higher priority)
 * ========================================================================== */

static void rb_rotate_left(struct uthread_internal *x)
{
    struct uthread_internal *y = x->rb_right;
    x->rb_right = y->rb_left;

    if (y->rb_left != NULL) {
        y->rb_left->rb_parent = x;
    }

    y->rb_parent = x->rb_parent;

    if (x->rb_parent == NULL) {
        g_cfs_state.rb_root = y;
    } else if (x == x->rb_parent->rb_left) {
        x->rb_parent->rb_left = y;
    } else {
        x->rb_parent->rb_right = y;
    }

    y->rb_left = x;
    x->rb_parent = y;
}

static void rb_rotate_right(struct uthread_internal *y)
{
    struct uthread_internal *x = y->rb_left;
    y->rb_left = x->rb_right;

    if (x->rb_right != NULL) {
        x->rb_right->rb_parent = y;
    }

    x->rb_parent = y->rb_parent;

    if (y->rb_parent == NULL) {
        g_cfs_state.rb_root = x;
    } else if (y == y->rb_parent->rb_left) {
        y->rb_parent->rb_left = x;
    } else {
        y->rb_parent->rb_right = x;
    }

    x->rb_right = y;
    y->rb_parent = x;
}

static void rb_insert_fixup(struct uthread_internal *z)
{
    while (z->rb_parent != NULL && z->rb_parent->rb_color == RB_RED) {
        if (z->rb_parent == z->rb_parent->rb_parent->rb_left) {
            struct uthread_internal *y = z->rb_parent->rb_parent->rb_right;
            if (y != NULL && y->rb_color == RB_RED) {
                z->rb_parent->rb_color = RB_BLACK;
                y->rb_color = RB_BLACK;
                z->rb_parent->rb_parent->rb_color = RB_RED;
                z = z->rb_parent->rb_parent;
            } else {
                if (z == z->rb_parent->rb_right) {
                    z = z->rb_parent;
                    rb_rotate_left(z);
                }
                z->rb_parent->rb_color = RB_BLACK;
                z->rb_parent->rb_parent->rb_color = RB_RED;
                rb_rotate_right(z->rb_parent->rb_parent);
            }
        } else {
            struct uthread_internal *y = z->rb_parent->rb_parent->rb_left;
            if (y != NULL && y->rb_color == RB_RED) {
                z->rb_parent->rb_color = RB_BLACK;
                y->rb_color = RB_BLACK;
                z->rb_parent->rb_parent->rb_color = RB_RED;
                z = z->rb_parent->rb_parent;
            } else {
                if (z == z->rb_parent->rb_left) {
                    z = z->rb_parent;
                    rb_rotate_right(z);
                }
                z->rb_parent->rb_color = RB_BLACK;
                z->rb_parent->rb_parent->rb_color = RB_RED;
                rb_rotate_left(z->rb_parent->rb_parent);
            }
        }
    }

    g_cfs_state.rb_root->rb_color = RB_BLACK;
}

void rb_insert(struct uthread_internal *thread)
{
    struct uthread_internal *y = NULL;
    struct uthread_internal *x = g_cfs_state.rb_root;

    /* Find insertion point */
    while (x != NULL) {
        y = x;
        if (thread->vruntime < x->vruntime) {
            x = x->rb_left;
        } else {
            x = x->rb_right;
        }
    }

    thread->rb_parent = y;
    thread->rb_left = NULL;
    thread->rb_right = NULL;
    thread->rb_color = RB_RED;

    if (y == NULL) {
        g_cfs_state.rb_root = thread;
    } else if (thread->vruntime < y->vruntime) {
        y->rb_left = thread;
    } else {
        y->rb_right = thread;
    }

    /* Update leftmost cache */
    if (g_cfs_state.rb_leftmost == NULL ||
        thread->vruntime < g_cfs_state.rb_leftmost->vruntime) {
        g_cfs_state.rb_leftmost = thread;
    }

    rb_insert_fixup(thread);
    g_cfs_state.count++;
}

static void rb_transplant(struct uthread_internal *u, struct uthread_internal *v)
{
    if (u->rb_parent == NULL) {
        g_cfs_state.rb_root = v;
    } else if (u == u->rb_parent->rb_left) {
        u->rb_parent->rb_left = v;
    } else {
        u->rb_parent->rb_right = v;
    }

    if (v != NULL) {
        v->rb_parent = u->rb_parent;
    }
}

static struct uthread_internal *rb_minimum(struct uthread_internal *x)
{
    while (x->rb_left != NULL) {
        x = x->rb_left;
    }
    return x;
}

static void rb_remove_fixup(struct uthread_internal *x, struct uthread_internal *x_parent)
{
    while (x != g_cfs_state.rb_root && (x == NULL || x->rb_color == RB_BLACK)) {
        if (x == x_parent->rb_left) {
            struct uthread_internal *w = x_parent->rb_right;
            if (w != NULL && w->rb_color == RB_RED) {
                w->rb_color = RB_BLACK;
                x_parent->rb_color = RB_RED;
                rb_rotate_left(x_parent);
                w = x_parent->rb_right;
            }
            if (w == NULL ||
                ((w->rb_left == NULL || w->rb_left->rb_color == RB_BLACK) &&
                 (w->rb_right == NULL || w->rb_right->rb_color == RB_BLACK))) {
                if (w != NULL) w->rb_color = RB_RED;
                x = x_parent;
                x_parent = x->rb_parent;
            } else {
                if (w->rb_right == NULL || w->rb_right->rb_color == RB_BLACK) {
                    if (w->rb_left != NULL) w->rb_left->rb_color = RB_BLACK;
                    w->rb_color = RB_RED;
                    rb_rotate_right(w);
                    w = x_parent->rb_right;
                }
                w->rb_color = x_parent->rb_color;
                x_parent->rb_color = RB_BLACK;
                if (w->rb_right != NULL) w->rb_right->rb_color = RB_BLACK;
                rb_rotate_left(x_parent);
                x = g_cfs_state.rb_root;
                break;
            }
        } else {
            struct uthread_internal *w = x_parent->rb_left;
            if (w != NULL && w->rb_color == RB_RED) {
                w->rb_color = RB_BLACK;
                x_parent->rb_color = RB_RED;
                rb_rotate_right(x_parent);
                w = x_parent->rb_left;
            }
            if (w == NULL ||
                ((w->rb_right == NULL || w->rb_right->rb_color == RB_BLACK) &&
                 (w->rb_left == NULL || w->rb_left->rb_color == RB_BLACK))) {
                if (w != NULL) w->rb_color = RB_RED;
                x = x_parent;
                x_parent = x->rb_parent;
            } else {
                if (w->rb_left == NULL || w->rb_left->rb_color == RB_BLACK) {
                    if (w->rb_right != NULL) w->rb_right->rb_color = RB_BLACK;
                    w->rb_color = RB_RED;
                    rb_rotate_left(w);
                    w = x_parent->rb_left;
                }
                w->rb_color = x_parent->rb_color;
                x_parent->rb_color = RB_BLACK;
                if (w->rb_left != NULL) w->rb_left->rb_color = RB_BLACK;
                rb_rotate_right(x_parent);
                x = g_cfs_state.rb_root;
                break;
            }
        }
    }

    if (x != NULL) x->rb_color = RB_BLACK;
}

void rb_remove(struct uthread_internal *z)
{
    if (z == NULL) return;

    struct uthread_internal *y = z;
    struct uthread_internal *x;
    struct uthread_internal *x_parent;
    int y_original_color = y->rb_color;

    if (z->rb_left == NULL) {
        x = z->rb_right;
        x_parent = z->rb_parent;
        rb_transplant(z, z->rb_right);
    } else if (z->rb_right == NULL) {
        x = z->rb_left;
        x_parent = z->rb_parent;
        rb_transplant(z, z->rb_left);
    } else {
        y = rb_minimum(z->rb_right);
        y_original_color = y->rb_color;
        x = y->rb_right;

        if (y->rb_parent == z) {
            x_parent = y;
        } else {
            x_parent = y->rb_parent;
            rb_transplant(y, y->rb_right);
            y->rb_right = z->rb_right;
            y->rb_right->rb_parent = y;
        }

        rb_transplant(z, y);
        y->rb_left = z->rb_left;
        y->rb_left->rb_parent = y;
        y->rb_color = z->rb_color;
    }

    /* Update leftmost cache */
    if (z == g_cfs_state.rb_leftmost) {
        g_cfs_state.rb_leftmost = g_cfs_state.rb_root ?
            rb_minimum(g_cfs_state.rb_root) : NULL;
    }

    if (y_original_color == RB_BLACK && g_cfs_state.rb_root != NULL) {
        rb_remove_fixup(x, x_parent);
    }

    /* Clear removed node's tree links */
    z->rb_parent = NULL;
    z->rb_left = NULL;
    z->rb_right = NULL;

    g_cfs_state.count--;
}

struct uthread_internal *rb_leftmost(void)
{
    return g_cfs_state.rb_leftmost;
}

/* ==========================================================================
 * CFS Scheduler Implementation
 * ========================================================================== */

static int cfs_init(void)
{
    memset(&g_cfs_state, 0, sizeof(g_cfs_state));
    return 0;
}

static void cfs_shutdown(void)
{
    memset(&g_cfs_state, 0, sizeof(g_cfs_state));
}

static void cfs_enqueue(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* For new threads, set vruntime to min_vruntime for fairness */
    if (thread->vruntime == 0) {
        thread->vruntime = g_cfs_state.min_vruntime;
    }

    /* Ensure vruntime is at least min_vruntime */
    if (thread->vruntime < g_cfs_state.min_vruntime) {
        thread->vruntime = g_cfs_state.min_vruntime;
    }

    rb_insert(thread);

    /* Calculate timeslice based on weight and number of threads */
    int total_weight = 0;
    /* Approximate total weight - use count * average weight */
    total_weight = g_cfs_state.count * CFS_NICE_0_WEIGHT;
    if (total_weight == 0) total_weight = thread->weight;

    uint64_t slice = (CFS_TARGET_LATENCY_NS * thread->weight) / total_weight;
    if (slice < CFS_MIN_GRANULARITY_NS) {
        slice = CFS_MIN_GRANULARITY_NS;
    }
    thread->timeslice_remaining = slice;
}

static struct uthread_internal *cfs_dequeue(void)
{
    struct uthread_internal *thread = rb_leftmost();
    if (thread != NULL) {
        rb_remove(thread);
    }
    return thread;
}

static void cfs_remove(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Check if thread is in the tree by looking for it */
    struct uthread_internal *node = g_cfs_state.rb_root;
    bool found = false;

    /* Simple search - could be optimized */
    while (node != NULL) {
        if (node == thread) {
            found = true;
            break;
        }
        if (thread->vruntime < node->vruntime) {
            node = node->rb_left;
        } else {
            node = node->rb_right;
        }
    }

    if (found) {
        rb_remove(thread);
    }
}

static void cfs_on_yield(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Update vruntime based on time used */
    uint64_t now = get_time_ns();
    if (thread->start_time > 0) {
        uint64_t delta = now - thread->start_time;
        /* vruntime increases slower for higher-weight threads */
        thread->vruntime += (delta * CFS_NICE_0_WEIGHT) / thread->weight;
    }
}

static void cfs_on_tick(struct uthread_internal *thread, uint64_t elapsed_ns)
{
    if (thread == NULL) return;

    /* Update vruntime */
    thread->vruntime += (elapsed_ns * CFS_NICE_0_WEIGHT) / thread->weight;

    /* Update min_vruntime */
    if (thread->vruntime > g_cfs_state.min_vruntime) {
        /* min_vruntime should be the minimum of all runnable threads */
        /* For simplicity, we approximate by tracking current thread */
        g_cfs_state.min_vruntime = thread->vruntime;
    }

    /* Decrease remaining timeslice */
    if (thread->timeslice_remaining > elapsed_ns) {
        thread->timeslice_remaining -= elapsed_ns;
    } else {
        thread->timeslice_remaining = 0;
    }
}

static bool cfs_should_preempt(struct uthread_internal *current)
{
    if (current == NULL) return false;

    /* Check if timeslice exhausted */
    if (current->timeslice_remaining == 0) {
        return g_cfs_state.count > 0;
    }

    /* Check if a thread with much lower vruntime is waiting */
    struct uthread_internal *leftmost = rb_leftmost();
    if (leftmost != NULL) {
        /* Preempt if leftmost has significantly lower vruntime */
        uint64_t delta = current->vruntime - leftmost->vruntime;
        if (delta > CFS_MIN_GRANULARITY_NS) {
            return true;
        }
    }

    return false;
}

static void cfs_update_priority(struct uthread_internal *thread)
{
    if (thread == NULL) return;

    /* Update weight from nice value */
    thread->weight = nice_to_weight(thread->nice);

    /* If thread is in tree, remove and re-insert to update position */
    /* Actually, vruntime doesn't change, so position should be fine */
}

static const char *cfs_name(void)
{
    return "CFS";
}

/* Scheduler operations structure */
struct scheduler_ops sched_cfs_ops = {
    .init = cfs_init,
    .shutdown = cfs_shutdown,
    .enqueue = cfs_enqueue,
    .dequeue = cfs_dequeue,
    .remove = cfs_remove,
    .on_yield = cfs_on_yield,
    .on_tick = cfs_on_tick,
    .should_preempt = cfs_should_preempt,
    .update_priority = cfs_update_priority,
    .name = cfs_name
};
