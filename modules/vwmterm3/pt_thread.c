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
#include "../../protothread.h"
#include "../../sched.h"

/*
    maximum pipe chunks consumed per scheduler dispatch.  higher values
    give each vterm more throughput per turn at the cost of interleaving
    with other tasks; 4 is the sweet spot for typical console apps.
*/
#define VWMTERM_DRAIN_CHUNKS    4

pt_t vwmterm_thd(void * const env)
{
    vwm_t               *vwm;
    vk_window_t         *window;
    vterm_t             *vterm;
    ssize_t             bytes_read = 0;
    int                 i;
    int                 history_sz;
    int                 width, height;
    int                 offset;

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

        if(vwmterm_data->frozen)
        {
            pt_yield(ctx_vwmterm);
            continue;
        }

        bytes_read = 0;
        for(i = 0; i < VWMTERM_DRAIN_CHUNKS; i++)
        {
            bytes_read = vterm_read_pipe(vterm, 10);
            if(bytes_read <= 0) break;

            if(vwmterm_data->scroll_offset > 0)
            {
                vterm_wnd_size(vterm, &width, &height);
                history_sz = vterm_get_history_size(vterm);
                offset = history_sz - height - vwmterm_data->scroll_offset;
                if(offset < 0) offset = 0;
                vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
                    VTERM_WND_RENDER_ALL);
            }
            else
            {
                vterm_wnd_update(vterm, -1, 0, 0);
            }
            vwmterm_data->redraw_pending = 1;
        }

        if(bytes_read == 0)
        {
            if(vwmterm_data->redraw_pending)
            {
                vk_window_update(window);
                vk_screen_refresh(vwm->screen);
                vwmterm_data->redraw_pending = 0;
            }

            pt_yield(ctx_vwmterm);

            continue;
        }

        if(bytes_read == -1)
        {
            vwmterm_data->state = VWMTERM_STATE_EPIPE;
            break;
        }

        ctx_vwmterm->did_work = 1;
        pt_yield(ctx_vwmterm);
    }
    while(!(*ctx_vwmterm->shutdown));

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
