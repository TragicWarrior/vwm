/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007      Bryan Christ <bryan.christ@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *----------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>

#include "protothread.h"
#include "sched.h"

#define VWM_SCHED_SLOT_FREE         0
#define VWM_SCHED_SLOT_ACTIVE       1

#define VWM_SCHED_WATCHDOG_SECS     20

/*
    the scheduler exports clock_tick as the tick signal for clock.c.
    set to 1 when ppoll() returns by timeout; clock.c clears it after
    firing the "vwm-clock-tick" event.
*/
unsigned int    clock_tick = 0;

typedef struct
{
    pt_thread_t             pt_thread;
    vwm_sched_ctx_t         *ctx;
    pt_f_t                  user_func;
    uint32_t                priority;
    uint8_t                 state;
    uint8_t                 ref_bit;
}
vwm_sched_slot_t;

struct _vwm_sched_s
{
    protothread_t           pt_normal;
    protothread_t           pt_high;
    vwm_sched_slot_t        slots[VWM_SCHED_MAX_TASKS];
};

static void vwm_sched_watchdog(int signum);
static pt_t vwm_sched_trampoline(void * const env);

vwm_sched_t*
vwm_sched_init(void)
{
    vwm_sched_t     *sched;

    sched = calloc(1, sizeof(vwm_sched_t));
    if(sched == NULL) return NULL;

    sched->pt_normal = protothread_create();
    sched->pt_high = protothread_create();

    return sched;
}

void
vwm_sched_deinit(vwm_sched_t *sched)
{
    if(sched == NULL) return;

    protothread_free(sched->pt_normal);
    protothread_free(sched->pt_high);

    free(sched);
}

int
vwm_sched_task_create(vwm_sched_t *sched, vwm_sched_ctx_t *ctx,
    pt_f_t func, uint32_t priority)
{
    vwm_sched_slot_t    *slot = NULL;
    protothread_t       pt;
    int                 i;

    if(sched == NULL || ctx == NULL || func == NULL) return -1;

    /* find a free slot */
    for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
    {
        if(sched->slots[i].state == VWM_SCHED_SLOT_FREE)
        {
            slot = &sched->slots[i];
            break;
        }
    }

    if(slot == NULL) return -1;

    memset(slot, 0, sizeof(*slot));
    slot->ctx = ctx;
    slot->user_func = func;
    slot->priority = priority;
    slot->state = VWM_SCHED_SLOT_ACTIVE;
    slot->ref_bit = 0;

    ctx->_sched_slot = slot;
    ctx->did_work = 0;

    pt = (priority == VWM_SCHED_HIGH) ? sched->pt_high : sched->pt_normal;

    /*
        pt_create links the thread onto the library's ready list.  the
        trampoline will be called (not the user's function) so the
        scheduler can sample did_work and detect PT_DONE.
    */
    pt_create_thread(pt, &slot->pt_thread, &ctx->pt_func,
        vwm_sched_trampoline, ctx);

    return 0;
}

void
vwm_sched_run(vwm_sched_t *sched, int *shutdown)
{
    struct timespec     now;
    struct timespec     last_tick;
    struct timespec     timeout;
    struct pollfd       pfds[1];
    sigset_t            pollmask;
    long                elapsed_ms;
    long                remaining_ms;
    int                 n_active;
    int                 sweep_had_work;
    int                 sweep_pos;
    int                 watchdog_armed;
    int                 normal_busy;
    int                 i;

    if(sched == NULL || shutdown == NULL) return;

    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;

    sigemptyset(&pollmask);

    sweep_had_work = 0;
    sweep_pos = 0;
    watchdog_armed = 0;

    clock_gettime(CLOCK_MONOTONIC, &last_tick);

    for(;;)
    {
        /* count active slots */
        n_active = 0;
        for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
        {
            if(sched->slots[i].state == VWM_SCHED_SLOT_ACTIVE) n_active++;
        }

        /* arm watchdog on first observation of shutdown */
        if(*shutdown && !watchdog_armed)
        {
            signal(SIGALRM, vwm_sched_watchdog);
            alarm(VWM_SCHED_WATCHDOG_SECS);
            watchdog_armed = 1;
        }

        if(n_active == 0) break;

        /*
            drive the clock tick from wall time, not from ppoll.  under
            sustained activity the sweep is always "had work" and we
            never enter the idle sleep path, but the panel (spinner,
            date) must still advance.  elapsed_ms is reused below when
            deciding how long to sleep on an idle sweep.
        */
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - last_tick.tv_sec) * 1000L
                   + (now.tv_nsec - last_tick.tv_nsec) / 1000000L;

        if(elapsed_ms >= VWM_SCHED_TICK_MS)
        {
            clock_tick = 1;
            last_tick = now;
            elapsed_ms = 0;
        }

        /*
            one scheduler step.  priority interleave: run one HIGH task
            and one NORMAL task.  when either queue is empty,
            protothread_run() is a cheap no-op.
        */
        protothread_run(sched->pt_high);
        protothread_run(sched->pt_normal);

        /*
            harvest ref bits set by the trampoline.  note whether any
            NORMAL slot was busy so we can grant the queue a single
            bonus run below.
        */
        normal_busy = 0;
        for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
        {
            if(sched->slots[i].ref_bit)
            {
                sweep_had_work = 1;
                if(sched->slots[i].priority == VWM_SCHED_NORMAL)
                    normal_busy = 1;
                sched->slots[i].ref_bit = 0;
            }
        }

        /*
            bonus run: when the NORMAL task that just ran set its ref
            bit, give the NORMAL queue one additional dispatch this
            step.  doubles throughput under sustained load (e.g.,
            heavy vterm output) without starving cold tasks, which
            still get their normal turn every revolution.
        */
        if(normal_busy)
        {
            protothread_run(sched->pt_normal);

            for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
            {
                if(sched->slots[i].ref_bit)
                {
                    sweep_had_work = 1;
                    sched->slots[i].ref_bit = 0;
                }
            }
        }

        sweep_pos++;

        /* end of revolution -- decide whether to sleep */
        if(sweep_pos >= n_active)
        {
            if(!sweep_had_work && !*shutdown)
            {
                /* sleep only until the next tick is due */
                remaining_ms = VWM_SCHED_TICK_MS - elapsed_ms;
                if(remaining_ms < 0) remaining_ms = 0;

                timeout.tv_sec = 0;
                timeout.tv_nsec = remaining_ms * 1000000L;

                ppoll(pfds, 1, &timeout, &pollmask);
                /* tick detection happens at the top of next iteration */
            }

            sweep_had_work = 0;
            sweep_pos = 0;
        }
    }

    /* all tasks drained; cancel watchdog if we beat it */
    if(watchdog_armed) alarm(0);
}

int
vwm_sched_active_count(vwm_sched_t *sched)
{
    int     n = 0;
    int     i;

    if(sched == NULL) return 0;

    for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
    {
        if(sched->slots[i].state == VWM_SCHED_SLOT_ACTIVE) n++;
    }

    return n;
}

void
vwm_sched_dump(vwm_sched_t *sched)
{
#ifdef _DEBUG
    vwm_sched_slot_t    *slot;
    int                 i;

    if(sched == NULL) return;

    openlog("vwm", LOG_PID, LOG_USER);
    syslog(LOG_DEBUG, "scheduler dump: max=%d", VWM_SCHED_MAX_TASKS);

    for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
    {
        slot = &sched->slots[i];
        if(slot->state == VWM_SCHED_SLOT_FREE) continue;

        syslog(LOG_DEBUG,
            "  slot %2d: %s func=%p ref=%d did_work=%d",
            i,
            (slot->priority == VWM_SCHED_HIGH) ? "HIGH  " : "NORMAL",
            (void *)slot->user_func,
            slot->ref_bit,
            slot->ctx ? slot->ctx->did_work : 0);
    }
#else
    (void)sched;
#endif
}

/*
    trampoline invoked by the protothread library on the scheduler's
    behalf.  calls the user's function and inspects its return:

      PT_WAIT -- task yielded.  ctx is still valid; harvest did_work
                 into the slot's ref bit and clear it.

      PT_DONE -- task finished.  the task has already freed ctx (per
                 the established vwmterm pattern), so do NOT touch
                 ctx.  mark the slot free.
*/
static pt_t
vwm_sched_trampoline(void * const env)
{
    vwm_sched_ctx_t     *ctx = env;
    vwm_sched_slot_t    *slot = ctx->_sched_slot;
    pt_t                result;

    result = slot->user_func(env);

    if(result.pt_rv == PT_RETURN_WAIT)
    {
        if(ctx->did_work)
        {
            slot->ref_bit = 1;
            ctx->did_work = 0;
        }
    }
    else
    {
        /* ctx may already be freed -- do not dereference */
        slot->ctx = NULL;
        slot->user_func = NULL;
        slot->ref_bit = 0;
        slot->state = VWM_SCHED_SLOT_FREE;
    }

    return result;
}

static void
vwm_sched_watchdog(int signum)
{
    (void)signum;
    _exit(1);
}
