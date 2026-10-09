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

#include <unistd.h>

#include <ncursesw/curses.h>

#include <sys/types.h>

#include <vdk.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"
#include "pt_thread.h"

#include "../../vwm.h"
#include "../../private.h"
#include "../../winman.h"
#include "../../sched.h"

/*
    maximum pipe chunks consumed per scheduler dispatch.  higher values
    give each vterm more throughput per turn at the cost of interleaving
    with other tasks; 4 is the sweet spot for typical console apps.

    each chunk only parses (vterm_read_pipe updates the cell grid).  the
    offscreen WINDOW is painted once after the drain, not per chunk --
    intermediate paints would be overwritten before the user-visible
    composite (E2).
*/
#define VWMTERM_DRAIN_CHUNKS    4

/*
    Keep the scheduler's wake set in step with whether this terminal is
    reading its pty.

    While it reads, the pty is registered: the scheduler wakes this task
    the moment the child writes, and the task never waits on the pty
    itself (see the zero timeout below).  While it does not read --
    frozen for SELECT mode -- the pty has to come out: unread output
    would leave it readable for good and the scheduler would never
    sleep.  `want` false also serves as the final unregister at exit.
*/
static void
vwmterm_wake_sync(vwmterm_data_t *vwmterm_data, int want)
{
    extern vwm_sched_t  *sched;
    int                 fd;

    if(want)
    {
        if(vwmterm_data->wake_fd >= 0) return;

        fd = vterm_get_pty_fd(vwmterm_data->vterm);
        if(fd < 0) return;

        if(vwm_sched_wake_fd_add(sched, fd,
            (vwm_sched_ctx_t *)vwmterm_data->sched_ctx) == 0)
        {
            vwmterm_data->wake_fd = fd;
        }
    }
    else
    {
        if(vwmterm_data->wake_fd < 0) return;

        vwm_sched_wake_fd_del(sched, vwmterm_data->wake_fd);
        vwmterm_data->wake_fd = -1;
    }
}

/* see pt_thread.h */
void
vwmterm_wake(void *data)
{
    extern vwm_sched_t  *sched;
    vwmterm_data_t      *vwmterm_data = (vwmterm_data_t *)data;

    if(vwmterm_data == NULL) return;

    vwm_sched_wake(sched, (vwm_sched_ctx_t *)vwmterm_data->sched_ctx);
}

pt_t vwmterm_thd(void * const env)
{
    vwm_t               *vwm;
    vk_window_t         *window;
    vterm_t             *vterm;
    ssize_t             bytes_read = 0;
    int                 i;
    int                 got_data;

    vwm_sched_ctx_t     *ctx_vwmterm;
    vwmterm_data_t      *vwmterm_data;

    ctx_vwmterm = (vwm_sched_ctx_t *)env;
    vwmterm_data = (vwmterm_data_t *)ctx_vwmterm->anything;
    window = vwmterm_data->window;
    vterm = vwmterm_data->vterm;
    vwm = vwm_get_instance();

    pt_resume(ctx_vwmterm);

    do
    {
        if(vwmterm_data->state == VWMTERM_STATE_EXITING) break;

        /* registered while reading, not while frozen */
        vwmterm_wake_sync(vwmterm_data, !vwmterm_data->frozen);

        /* frozen: nothing to do until SELECT mode ends, which wakes
           this task (vwmterm_wake); the heartbeat is the fallback */
        if(vwmterm_data->frozen)
        {
            vwm_sched_wait(ctx_vwmterm);
            continue;
        }

        bytes_read = 0;
        got_data = 0;
        for(i = 0; i < VWMTERM_DRAIN_CHUNKS; i++)
        {
            /* timeout 0: take what is there and return.  The waiting
               is the scheduler's job: its ppoll() watches this pty
               (registered just above) along with everything else. */
            bytes_read = vterm_read_pipe(vterm, 0);
            if(bytes_read <= 0) break;

            got_data = 1;
            vwmterm_data->redraw_pending = 1;
        }

        if(bytes_read == -1)
        {
            vwmterm_data->state = VWMTERM_STATE_EPIPE;
            break;
        }

        /*
            one cell->WINDOW paint for every chunk parsed this turn.
            dirty bits accumulate across the drain; a single
            vterm_wnd_update (or scrollback full-render) covers them.
        */
        if(got_data)
        {
            if(vwmterm_data->scroll_offset > 0)
            {
                vterm_wnd_scrollback(vterm, vwmterm_data->scroll_offset,
                    VTERM_WND_RENDER_ALL);
            }
            else
            {
                vterm_wnd_update(vterm, -1, 0, 0);
            }
        }

        if(bytes_read == 0)
        {
            if(vwmterm_data->redraw_pending)
            {
                /* push this tile's window (cheap), but defer the screen
                   composite: mark it dirty and let the scheduler's
                   per-step hook coalesce every busy tile into a single
                   vk_screen_refresh (item 5). */
                vwmterm_window_update(vwmterm_data);
                vwm->screen_dirty = 1;
                vwmterm_data->redraw_pending = 0;
            }

            /* the pty is drained: wait until the child writes again.
               (vterm_read_pipe also reaps the child, so the heartbeat's
               periodic wake is what notices a program that exited
               without closing the pty.) */
            vwm_sched_wait(ctx_vwmterm);

            continue;
        }

        /* saturated drain (all VWMTERM_DRAIN_CHUNKS produced data):
           WINDOW is current from the paint above; leave redraw_pending
           set so a later dry turn still does the window_update +
           screen_dirty hand-off.  There is more to read, so yield
           rather than wait: stay ready, but let the others have a
           turn first. */
        pt_yield(ctx_vwmterm);
    }
    while(!(*ctx_vwmterm->shutdown));

    /* the pty is about to be closed: take it out of the wake set */
    vwmterm_wake_sync(vwmterm_data, 0);

    if(*ctx_vwmterm->shutdown || bytes_read == -1)
    {
        vwm_default_WINDOW_CLOSE(VK_WIDGET(window));
    }

    if(vwmterm_data->vterm != NULL)
        vterm_destroy(vwmterm_data->vterm);

    free(vwmterm_data);
    free(ctx_vwmterm);

    return PT_DONE;
}
