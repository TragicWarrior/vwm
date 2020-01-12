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

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <sys/types.h>

#include <viper.h>
#include <vterm.h>
// #include <protothread.h>

#include "vwmterm.h"
#include "events.h"
#include "pt_thread.h"

#include "../../vwm.h"
#include "../../private.h"
#include "../../protothread.h"

pt_t vwmterm_thd(void * const env)
{
    vwnd_t          *vwnd;
    vterm_t         *vterm;
    ssize_t         bytes_read;
    ssize_t         total_bytes = 0;

    pt_context_t    *ctx_vwmterm;
    vwmterm_data_t  *vwmterm_data;

    // the stack gets lost on every iteration so we need to copy
    ctx_vwmterm = (pt_context_t *)env;
    vwmterm_data = (vwmterm_data_t *)ctx_vwmterm->anything;
    vwnd = vwmterm_data->vwnd;
    vterm = vwmterm_data->vterm;

    pt_resume(ctx_vwmterm);

    do
    {
        // check to see if thread is exiting
        if(vwmterm_data->state == VWMTERM_STATE_EXITING) break;

        bytes_read = vterm_read_pipe(vterm, 10);

        if(bytes_read == 0)
        {
            if(total_bytes > 0) viper_window_redraw(vwnd);

            pt_yield(ctx_vwmterm);

            continue;
        }

        // handle pipe error condition
        if(bytes_read == -1)
        {
            vwmterm_data->state = VWMTERM_STATE_EPIPE;
            break;
        }

        if(bytes_read > 0)
        {
            vterm_wnd_update(vterm, -1, 0, 0);
            total_bytes += bytes_read;
        }
    }
    while(!(*ctx_vwmterm->shutdown));

    /*
        call for a window close *only* if VWM is shutting down
        or there was a pipe error.
    */
    if(*ctx_vwmterm->shutdown || bytes_read == -1)
    {
        viper_window_close(vwnd);
    }

    vterm_destroy(vwmterm_data->vterm);

    free(vwmterm_data);
    free(ctx_vwmterm);

    return PT_DONE;
}
