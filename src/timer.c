/**
 * LibUThread Timer and Preemption
 *
 * Implements preemptive scheduling using SIGALRM/SIGVTALRM.
 *
 * @file timer.c
 */

#define _GNU_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

/* Preemption state */
static volatile sig_atomic_t s_preemption_disabled = 0;
static volatile sig_atomic_t s_preempt_pending = 0;
static volatile sig_atomic_t s_timer_active = 0;

/* ==========================================================================
 * Signal Handler
 * ========================================================================== */

/**
 * Timer signal handler for preemption.
 *
 * Called when SIGALRM fires. Triggers scheduler if preemption
 * is enabled and not in a critical section.
 */
static void timer_signal_handler(int signum)
{
    (void)signum;

    if (!g_scheduler.initialized) {
        return;
    }

    /* If preemption disabled, just mark it pending */
    if (s_preemption_disabled > 0) {
        s_preempt_pending = 1;
        return;
    }

    /* If already in scheduler, don't recurse */
    if (g_scheduler.in_scheduler) {
        return;
    }

    /* Check if current thread is in critical section */
    struct uthread_internal *current = g_scheduler.current;
    if (current != NULL && current->in_critical_section) {
        s_preempt_pending = 1;
        return;
    }

    /* Trigger scheduler tick */
    scheduler_tick();
}

/* ==========================================================================
 * Timer Management
 * ========================================================================== */

int timer_init(void)
{
    /* Set up signal handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_signal_handler;
    sa.sa_flags = SA_RESTART;

    /* Block all signals during handler */
    sigfillset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, &g_scheduler.old_sigaction) == -1) {
        perror("sigaction");
        return -1;
    }

    /* Set up signal mask for blocking during critical sections */
    sigemptyset(&g_scheduler.block_mask);
    sigaddset(&g_scheduler.block_mask, SIGALRM);

    s_preemption_disabled = 0;
    s_preempt_pending = 0;
    s_timer_active = 0;

    UTHREAD_DEBUG("Timer initialized");

    return 0;
}

void timer_shutdown(void)
{
    /* Stop timer first */
    timer_stop();

    /* Restore old signal handler */
    sigaction(SIGALRM, &g_scheduler.old_sigaction, NULL);

    UTHREAD_DEBUG("Timer shutdown");
}

void timer_start(void)
{
    if (s_timer_active) {
        return;
    }

    /* Set up interval timer */
    struct itimerval itv;
    uint64_t ns = g_scheduler.timeslice_ns;

    /* Convert nanoseconds to seconds + microseconds */
    itv.it_interval.tv_sec = ns / 1000000000ULL;
    itv.it_interval.tv_usec = (ns % 1000000000ULL) / 1000ULL;
    itv.it_value = itv.it_interval;

    /* Use ITIMER_REAL for SIGALRM */
    if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
        perror("setitimer");
        return;
    }

    s_timer_active = 1;

    UTHREAD_DEBUG("Timer started (interval=%lu us)",
                  (unsigned long)itv.it_interval.tv_usec);
}

void timer_stop(void)
{
    if (!s_timer_active) {
        return;
    }

    /* Disable timer */
    struct itimerval itv;
    memset(&itv, 0, sizeof(itv));

    setitimer(ITIMER_REAL, &itv, NULL);

    s_timer_active = 0;

    UTHREAD_DEBUG("Timer stopped");
}

void timer_set_interval(uint64_t ns)
{
    bool was_active = s_timer_active;

    if (was_active) {
        timer_stop();
    }

    g_scheduler.timeslice_ns = ns;

    if (was_active) {
        timer_start();
    }
}

/* ==========================================================================
 * Preemption Control
 * ========================================================================== */

void preemption_disable(void)
{
    /* Block SIGALRM */
    sigprocmask(SIG_BLOCK, &g_scheduler.block_mask, NULL);
    s_preemption_disabled++;
}

void preemption_enable(void)
{
    if (s_preemption_disabled == 0) {
        return;
    }

    s_preemption_disabled--;

    if (s_preemption_disabled == 0) {
        /* Unblock SIGALRM */
        sigprocmask(SIG_UNBLOCK, &g_scheduler.block_mask, NULL);

        /* Check if preemption was pending */
        if (s_preempt_pending) {
            s_preempt_pending = 0;

            /* Don't preempt if in scheduler or current is in critical section */
            if (!g_scheduler.in_scheduler) {
                struct uthread_internal *current = g_scheduler.current;
                if (current == NULL || !current->in_critical_section) {
                    scheduler_tick();
                }
            }
        }
    }
}

bool preemption_is_enabled(void)
{
    return (s_preemption_disabled == 0);
}
