#ifndef _VWM_SCHED_H_
#define _VWM_SCHED_H_

#include <inttypes.h>

#include "protothread.h"

/*
    compile-time cap on the number of concurrent tasks.  task creation
    fails when the ring is full.
*/
#ifndef VWM_SCHED_MAX_TASKS
#define VWM_SCHED_MAX_TASKS         20
#endif

/*
    ppoll() timeout that drives the clock tick.  when ppoll() returns
    by timeout (no input), the driver marks the clock slot active and
    fires the "vwm-clock-tick" event.
*/
#define VWM_SCHED_TICK_MS           100

/*
    priority classes.  a single ring is walked by a "main hand".  a
    second "priority hand" interleaves one HIGH task between every
    advance of the main hand, so with a single HIGH slot, that slot
    runs on every scheduler step.
*/
enum
{
    VWM_SCHED_NORMAL        =   0x00,
    VWM_SCHED_HIGH          =   0x01
};

/*
    base context embedded in every task's env struct.  the protothread
    library requires a pt_func_t named 'pt_func' at a fixed offset in
    whatever struct is passed as env; this shape satisfies that.

    did_work:   task sets this to 1 when it did something useful on
                its latest run.  the scheduler reads and clears it
                after the task returns, treating it as the Clock
                reference bit.  when a full sweep completes with no
                bits set, the driver blocks in ppoll() until input
                arrives or the tick timeout expires.

    shutdown:   pointer to the process-wide shutdown flag.  tasks
                inspect it to exit cleanly.

    anything:   user pointer for task-specific data.
*/
typedef struct _vwm_sched_ctx_s
{
    pt_func_t               pt_func;

    int                     did_work;

    int                     *shutdown;
    void                    *anything;

    /* scheduler use only; do not touch from task code */
    void                    *_sched_slot;
}
vwm_sched_ctx_t;

/* opaque scheduler instance */
typedef struct _vwm_sched_s     vwm_sched_t;

/*
    lifecycle
*/
vwm_sched_t*    vwm_sched_init(void);
void            vwm_sched_deinit(vwm_sched_t *sched);

/*
    register a task.  'ctx' must remain valid for the task's lifetime
    (the task typically frees it just before returning PT_DONE).
    'priority' is VWM_SCHED_NORMAL or VWM_SCHED_HIGH.

    returns 0 on success, -1 if the ring is full.
*/
int             vwm_sched_task_create(vwm_sched_t *sched,
                    vwm_sched_ctx_t *ctx, pt_f_t func, uint32_t priority);

/*
    main loop.  owns the ppoll() on STDIN_FILENO with a
    VWM_SCHED_TICK_MS timeout.  returns when every slot has returned
    PT_DONE.  if '*shutdown' becomes non-zero, arms alarm(20) as a
    watchdog: if tasks have not drained by then, the handler _exit()s.
*/
void            vwm_sched_run(vwm_sched_t *sched, int *shutdown);

/*
    optional per-step callback.  fired once at the end of every
    scheduler step, after all ready tasks have run.  vwm uses it to
    coalesce vterm screen composites: drain tasks mark the screen dirty
    and the callback issues a single vk_screen_refresh per step instead
    of one per busy tile.  pass cb = NULL to disable.  the scheduler
    stays vdk-agnostic -- it only invokes the hook, it does not composite.
*/
typedef void    (*vwm_sched_step_cb_t)(void *arg);
void            vwm_sched_set_step_cb(vwm_sched_t *sched,
                    vwm_sched_step_cb_t cb, void *arg);

/* extra fd included in the idle ppoll so a control-socket accept
   wakes the loop the same way stdin does.  -1 disables. */
void            vwm_sched_set_wake_fd(vwm_sched_t *sched, int fd);

/*
    number of slots currently in use (active tasks).  cheap O(N) scan
    of the ring; safe to call from anywhere on the main thread.
*/
int             vwm_sched_active_count(vwm_sched_t *sched);

/*
    debug.  when compiled with -D_DEBUG, dumps the ring state
    (priority, state, did_work, function pointer) to syslog at
    LOG_DEBUG.  no-op otherwise.
*/
void            vwm_sched_dump(vwm_sched_t *sched);

#endif
