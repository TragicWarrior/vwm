#ifndef _VWM_SCHED_H_
#define _VWM_SCHED_H_

#include <inttypes.h>

#include "protothread.h"

#if !defined(PT_VERSION_NUMBER) || !PT_VERSION_AT_LEAST(2, 0, 0)
#error vwm requires protothread 2.0.0 or later
#endif

/*
    The vwm scheduler: an event loop over protothread tasks.

    A task runs when it has something to do and waits when it does not.
    It never polls and never sleeps on its own: the scheduler does all
    the waiting, in one ppoll(), and wakes a task for one of three
    reasons:

      1. a descriptor the task registered is readable
         (vwm_sched_wake_fd_add)
      2. someone asked for it by name (vwm_sched_wake)
      3. the heartbeat: every VWM_SCHED_TICK_MS, and whenever a signal
         interrupts the sleep, every task is woken once

    So a wake-up is a hint, not a promise.  A task must check for work,
    do what there is, and wait again -- it may find nothing.  The
    heartbeat is what makes that safe to rely on: a wake-up that was
    missed, or a kind of event that has no descriptor (a child that
    exited, a key pushed back inside ncurses), is picked up within one
    tick.  It is also what drives the clock.

    Inside a task:

        vwm_sched_wait(ctx);    nothing to do -- run me when woken
        pt_yield(ctx);          more to do -- let the others have a turn
*/

/*
    compile-time cap on the number of concurrent tasks.  task creation
    fails when the table is full.
*/
#ifndef VWM_SCHED_MAX_TASKS
#define VWM_SCHED_MAX_TASKS         20
#endif

/* the heartbeat period: the clock tick, and the longest a task waits
   for a turn when nothing else wakes it */
#define VWM_SCHED_TICK_MS           100

/*
    priority classes.  each step of the loop runs one ready HIGH task
    and one ready NORMAL task, so a HIGH task (keyboard and mouse) gets
    a turn between every two NORMAL turns however many terminals are
    busy.
*/
enum
{
    VWM_SCHED_NORMAL        =   0x00,
    VWM_SCHED_HIGH          =   0x01
};

/*
    base context embedded in every task's env struct.  pt_create()
    requires a pt_func_t member named pt_func; this shape satisfies that.

    shutdown:   pointer to the process-wide shutdown flag.  tasks
                inspect it to exit cleanly.

    anything:   user pointer for task-specific data.
*/
typedef struct _vwm_sched_ctx_s
{
    pt_func_t               pt_func;

    int                     *shutdown;
    void                    *anything;

    /* scheduler use only; do not touch from task code */
    void                    *_sched_slot;
}
vwm_sched_ctx_t;

/*
    wait until woken (see the three reasons above).  only inside a task
    function, like pt_yield().  a task waits on its own context, which
    is how vwm_sched_wake() and the wake descriptors find it.
*/
#define vwm_sched_wait(ctx)         pt_wait((ctx), (ctx))

/* opaque scheduler instance */
typedef struct _vwm_sched_s     vwm_sched_t;

/*
    lifecycle
*/
vwm_sched_t*    vwm_sched_init(void);
void            vwm_sched_deinit(vwm_sched_t *sched);

/*
    register a task.  'ctx' may be freed just before returning PT_DONE.
    The pt_thread_t lives in the scheduler slot, not in ctx.
    'priority' is VWM_SCHED_NORMAL or VWM_SCHED_HIGH.  the task is ready
    at once and gets its first turn on the next step.

    returns 0 on success, -1 if the table is full.
*/
int             vwm_sched_task_create(vwm_sched_t *sched,
                    vwm_sched_ctx_t *ctx, pt_f_t func, uint32_t priority);

/*
    main loop.  returns when every task has returned PT_DONE.  if
    '*shutdown' becomes non-zero, arms alarm(20) as a watchdog: if tasks
    have not drained by then, the handler _exit()s.
*/
void            vwm_sched_run(vwm_sched_t *sched, int *shutdown);

/*
    optional per-step callback, fired once at the end of every step.
    vwm uses it to coalesce screen composites: tasks mark the screen
    dirty and the callback issues a single vk_screen_refresh per step
    instead of one per busy terminal.  it also services the control
    socket.  pass cb = NULL to disable.  the scheduler stays
    vdk-agnostic -- it only invokes the hook.
*/
typedef void    (*vwm_sched_step_cb_t)(void *arg);
void            vwm_sched_set_step_cb(vwm_sched_t *sched,
                    vwm_sched_step_cb_t cb, void *arg);

/*
    wake descriptors: the scheduler sleeps on these, and when one is
    readable it wakes the task that registered it.  ctx == NULL only
    ends the sleep (the step callback then looks at the descriptor; the
    control socket works this way).

    the task must read what is there.  a descriptor left readable keeps
    the scheduler from ever sleeping, so a task that stops reading (a
    terminal frozen for SELECT mode) takes its descriptor out and puts
    it back when it reads again.

    adding a descriptor that is already registered re-points it at ctx.
    add returns 0, or -1 when the table is full.  del of a descriptor
    that is not registered is a no-op.
*/
int             vwm_sched_wake_fd_add(vwm_sched_t *sched, int fd,
                    vwm_sched_ctx_t *ctx);
void            vwm_sched_wake_fd_del(vwm_sched_t *sched, int fd);

/*
    wake a task now, for work that arrived some other way than through a
    descriptor: a key pushed back with ungetch(), a terminal that was
    just unfrozen.  harmless if the task is not waiting.
*/
void            vwm_sched_wake(vwm_sched_t *sched, vwm_sched_ctx_t *ctx);

/*
    number of tasks.  cheap O(N) scan; safe to call from anywhere on the
    main thread.
*/
int             vwm_sched_active_count(vwm_sched_t *sched);

/*
    debug.  when compiled with -D_DEBUG, dumps the task table (priority,
    function pointer) and the wake descriptors to syslog at LOG_DEBUG.
    no-op otherwise.
*/
void            vwm_sched_dump(vwm_sched_t *sched);

#endif
