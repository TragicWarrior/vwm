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

/* the design is described at the top of sched.h */

#define VWM_SCHED_SLOT_FREE         0
#define VWM_SCHED_SLOT_ACTIVE       1

#define VWM_SCHED_WATCHDOG_SECS     20

/* room for a couple of wake descriptors per task (the input task has
   two: the keyboard and the mouse daemon) plus the control socket */
#define VWM_SCHED_MAX_WAKE_FDS      (VWM_SCHED_MAX_TASKS + 8)

/*
    set to 1 by the scheduler on each real tick (every
    VWM_SCHED_TICK_MS); clock.c clears it when it has done its per-tick
    work.  the clock task is woken by the heartbeat like every other
    task, and the heartbeat also fires when a signal interrupts the
    sleep, so "I was woken" is not quite "a tick passed" -- this flag is.
*/
unsigned int    clock_tick = 0;

/* one task */
typedef struct
{
    pt_thread_t             pt_thread;
    vwm_sched_ctx_t         *ctx;
    pt_f_t                  user_func;
    uint32_t                priority;
    uint8_t                 state;
    struct _vwm_sched_s     *owner;         /* the scheduler it belongs to */
}
vwm_sched_slot_t;

/* one wake descriptor: when fd is readable, wake ctx's task (ctx NULL:
   just end the sleep) */
typedef struct
{
    int                     fd;
    vwm_sched_ctx_t         *ctx;
}
vwm_sched_wake_t;

struct _vwm_sched_s
{
    /* two run queues, so a HIGH task gets a turn between NORMAL ones */
    protothread_t           pt_normal;
    protothread_t           pt_high;

    /* is anything ready to run on that queue?  kept exact from two
       sides: protothread_run() says so when it returns, and the ready
       hook below says so when a wake fills an empty queue.  the loop
       sleeps only when both are clear. */
    int                     ready_normal;
    int                     ready_high;

    vwm_sched_step_cb_t     step_cb;
    void                    *step_cb_arg;

    vwm_sched_wake_t        wake[VWM_SCHED_MAX_WAKE_FDS];
    int                     n_wake;

    vwm_sched_slot_t        slots[VWM_SCHED_MAX_TASKS];
};

static void vwm_sched_watchdog(int signum);
static pt_t vwm_sched_trampoline(void * const env);
static void vwm_sched_on_ready(env_t env);
static void vwm_sched_wake_all(vwm_sched_t *sched);

vwm_sched_t*
vwm_sched_init(void)
{
    vwm_sched_t     *sched;

    sched = calloc(1, sizeof(vwm_sched_t));
    if(sched == NULL) return NULL;

    sched->pt_normal = protothread_create();
    sched->pt_high = protothread_create();

    /* have each queue tell us when a wake makes it non-empty */
    protothread_set_ready_function(sched->pt_normal, vwm_sched_on_ready,
        &sched->ready_normal);
    protothread_set_ready_function(sched->pt_high, vwm_sched_on_ready,
        &sched->ready_high);

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

void
vwm_sched_set_step_cb(vwm_sched_t *sched, vwm_sched_step_cb_t cb, void *arg)
{
    if(sched == NULL) return;

    sched->step_cb = cb;
    sched->step_cb_arg = arg;
}

/*
    the protothread library calls this when a queue goes from empty to
    having a ready task -- a task was created, or a waiting one was
    woken from outside that queue's own run.  env is the queue's ready
    flag.
*/
static void
vwm_sched_on_ready(env_t env)
{
    *(int *)env = 1;
}

/* the run queue a task lives on */
static protothread_t
vwm_sched_queue_of(vwm_sched_t *sched, vwm_sched_slot_t *slot)
{
    return (slot->priority == VWM_SCHED_HIGH)
        ? sched->pt_high : sched->pt_normal;
}

/* see sched.h */
int
vwm_sched_wake_fd_add(vwm_sched_t *sched, int fd, vwm_sched_ctx_t *ctx)
{
    int     i;

    if(sched == NULL || fd < 0) return -1;

    for(i = 0; i < sched->n_wake; i++)
    {
        if(sched->wake[i].fd != fd) continue;

        sched->wake[i].ctx = ctx;           /* already there: re-point */
        return 0;
    }

    if(sched->n_wake >= VWM_SCHED_MAX_WAKE_FDS) return -1;

    sched->wake[sched->n_wake].fd = fd;
    sched->wake[sched->n_wake].ctx = ctx;
    sched->n_wake++;

    return 0;
}

/* see sched.h */
void
vwm_sched_wake_fd_del(vwm_sched_t *sched, int fd)
{
    int     i;

    if(sched == NULL) return;

    for(i = 0; i < sched->n_wake; i++)
    {
        if(sched->wake[i].fd != fd) continue;

        /* order does not matter: fill the hole with the last entry */
        sched->wake[i] = sched->wake[--sched->n_wake];
        return;
    }
}

/* see sched.h */
void
vwm_sched_wake(vwm_sched_t *sched, vwm_sched_ctx_t *ctx)
{
    vwm_sched_slot_t    *slot;

    if(sched == NULL || ctx == NULL) return;

    slot = ctx->_sched_slot;
    if(slot == NULL || slot->state != VWM_SCHED_SLOT_ACTIVE) return;

    /* a task waits on its own context (vwm_sched_wait); signalling a
       channel nobody waits on does nothing */
    pt_signal(vwm_sched_queue_of(sched, slot), ctx);
}

/*
    the heartbeat: give every task one turn.  this is what bounds how
    stale anything can get -- see sched.h.
*/
static void
vwm_sched_wake_all(vwm_sched_t *sched)
{
    int     i;

    for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
    {
        if(sched->slots[i].state != VWM_SCHED_SLOT_ACTIVE) continue;

        pt_signal(vwm_sched_queue_of(sched, &sched->slots[i]),
            sched->slots[i].ctx);
    }
}

int
vwm_sched_task_create(vwm_sched_t *sched, vwm_sched_ctx_t *ctx,
    pt_f_t func, uint32_t priority)
{
    vwm_sched_slot_t    *slot = NULL;
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
    slot->owner = sched;

    ctx->_sched_slot = slot;

    /*
        pt_create links the thread onto the queue's ready list (and so
        raises that queue's ready flag).  the trampoline is what gets
        called, not the user's function, so the scheduler can see
        PT_DONE and free the slot.
    */
    pt_create(vwm_sched_queue_of(sched, slot), &slot->pt_thread,
        vwm_sched_trampoline, ctx);

    return 0;
}

void
vwm_sched_run(vwm_sched_t *sched, vwm_shutdown_t *shutdown)
{
    struct timespec     now;
    struct timespec     last_tick;
    struct timespec     timeout;
    struct pollfd       pfds[VWM_SCHED_MAX_WAKE_FDS];
    vwm_sched_ctx_t     *pctx[VWM_SCHED_MAX_WAKE_FDS];
    nfds_t              n_pfds;
    sigset_t            pollmask;
    long                elapsed_ms;
    long                remaining_ms;
    int                 n_active;
    int                 watchdog_armed;
    int                 heartbeat;
    int                 rc;
    int                 i;

    if(sched == NULL || shutdown == NULL) return;

    sigemptyset(&pollmask);

    watchdog_armed = 0;

    clock_gettime(CLOCK_MONOTONIC, &last_tick);

    for(;;)
    {
        /* count the tasks; when the last one has finished, so have we */
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
            how long may we sleep?  not at all if a task is ready to
            run -- the ppoll() below then only looks at the descriptors,
            so a terminal that floods (and is therefore always ready)
            cannot keep the keyboard from being noticed.  otherwise
            until the next tick is due.
        */
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - last_tick.tv_sec) * 1000L
                   + (now.tv_nsec - last_tick.tv_nsec) / 1000000L;

        remaining_ms = VWM_SCHED_TICK_MS - elapsed_ms;
        if(remaining_ms < 0) remaining_ms = 0;
        if(sched->ready_high || sched->ready_normal) remaining_ms = 0;

        timeout.tv_sec = 0;
        timeout.tv_nsec = remaining_ms * 1000000L;

        /* the one place vwm waits: every wake descriptor at once.  the
           set is rebuilt each time because tasks add and remove theirs
           as they come and go. */
        n_pfds = 0;
        for(i = 0; i < sched->n_wake; i++)
        {
            pfds[n_pfds].fd = sched->wake[i].fd;
            pfds[n_pfds].events = POLLIN;
            pfds[n_pfds].revents = 0;
            pctx[n_pfds] = sched->wake[i].ctx;
            n_pfds++;
        }

        rc = ppoll(pfds, n_pfds, &timeout, &pollmask);

        heartbeat = 0;

        /* a signal cut the sleep short.  whatever it announced (a
           resize, a child that exited) has no descriptor to point at,
           so let every task look. */
        if(rc < 0 && errno == EINTR) heartbeat = 1;

        if(rc > 0)
        {
            for(i = 0; i < (int)n_pfds; i++)
            {
                /* closed without being taken out: it would report
                   POLLNVAL at once, every time, and turn the sleep
                   into a spin.  drop it. */
                if(pfds[i].revents & POLLNVAL)
                {
                    vwm_sched_wake_fd_del(sched, pfds[i].fd);
                    continue;
                }

                /* readable, or hung up (a terminal whose program
                   exited): its task has something to do */
                if(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
                    vwm_sched_wake(sched, pctx[i]);
            }
        }

        /* the tick, from wall time: under load the loop never sleeps a
           full period, but the clock must still advance */
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - last_tick.tv_sec) * 1000L
                   + (now.tv_nsec - last_tick.tv_nsec) / 1000000L;

        if(elapsed_ms >= VWM_SCHED_TICK_MS)
        {
            clock_tick = 1;
            last_tick = now;
            heartbeat = 1;
        }

        /* shutting down: tasks only see the flag when they run */
        if(*shutdown) heartbeat = 1;

        if(heartbeat) vwm_sched_wake_all(sched);

        /*
            one step: one ready HIGH task (keyboard and mouse), then one
            ready NORMAL task.  an empty queue makes protothread_run() a
            cheap no-op.  each call reports whether its queue still has
            a task ready, which decides above whether the next pass may
            sleep.
        */
        sched->ready_high = protothread_run(sched->pt_high);
        sched->ready_normal = protothread_run(sched->pt_normal);

        /*
            per-step hook: tasks that produced output marked the screen
            dirty rather than each compositing it; this turns a step's
            worth into one refresh, and services the control socket.
        */
        if(sched->step_cb != NULL) sched->step_cb(sched->step_cb_arg);
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
    syslog(LOG_DEBUG, "scheduler dump: max=%d ready high=%d normal=%d",
        VWM_SCHED_MAX_TASKS, sched->ready_high, sched->ready_normal);

    for(i = 0; i < VWM_SCHED_MAX_TASKS; i++)
    {
        slot = &sched->slots[i];
        if(slot->state == VWM_SCHED_SLOT_FREE) continue;

        syslog(LOG_DEBUG, "  slot %2d: %s func=%p ctx=%p",
            i,
            (slot->priority == VWM_SCHED_HIGH) ? "HIGH  " : "NORMAL",
            (void *)slot->user_func,
            (void *)slot->ctx);
    }

    for(i = 0; i < sched->n_wake; i++)
    {
        syslog(LOG_DEBUG, "  wake fd %d -> ctx=%p",
            sched->wake[i].fd, (void *)sched->wake[i].ctx);
    }
#else
    (void)sched;
#endif
}

/*
    trampoline invoked by the protothread library on the scheduler's
    behalf.  calls the user's function and inspects its return:

      anything but PT_DONE -- the task yielded or is waiting; nothing
                 to do.

      PT_DONE -- the task finished.  it has already freed ctx (per the
                 established vwmterm pattern), so do NOT dereference
                 ctx.  free the slot.  the pt_thread_t stays in the
                 slot: protothread_run() writes it after we return.
*/
static pt_t
vwm_sched_trampoline(void * const env)
{
    vwm_sched_ctx_t     *ctx = env;
    vwm_sched_slot_t    *slot = ctx->_sched_slot;
    pt_t                result;

    result = slot->user_func(env);

    if(result.pt_rv == PT_DONE.pt_rv)
    {
        vwm_sched_t     *sched = slot->owner;
        int             i;

        /* a wake descriptor the task left registered would go on waking
           the loop for a task that no longer exists.  ctx may already
           be freed -- its address is only compared, never followed. */
        for(i = sched->n_wake - 1; i >= 0; i--)
        {
            if(sched->wake[i].ctx == ctx)
                sched->wake[i] = sched->wake[--sched->n_wake];
        }

        slot->ctx = NULL;
        slot->user_func = NULL;
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
