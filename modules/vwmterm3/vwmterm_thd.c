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
#include <protothread.h>

#include "vwmterm.h"
#include "events.h"
#include "vwmterm_thd.h"
#include "../../vwm.h"


pt_t vwmterm_thd(void * const env)
{
    WINDOW          *window;
    vterm_t         *vterm;
    ssize_t         bytes_read;

    pt_context_t    *ctx_vwmterm;
    vwmterm_data_t  *vwmterm_data;

    ctx_vwmterm = (pt_context_t *)env;
    pt_resume(ctx_vwmterm);

    do
    {
        vwmterm_data = (vwmterm_data_t *)ctx_vwmterm->anything;

        // check to see if thread is exiting
        if(vwmterm_data->state == VWMTERM_STATE_EXITING) break;

        window = vwmterm_data->window;
        vterm = vwmterm_data->vterm;

        bytes_read = vterm_read_pipe(vterm);

        // handle pipe error condition
        if(bytes_read == -1)
        {
            viper_window_destroy(window);
            // TODO: destroy vterm, vwmterm_data
            break;
        }

        if(bytes_read > 0)
        {
            vterm_wnd_update(vterm);
            viper_window_redraw(window);
        }

        pt_yield(ctx_vwmterm);
    }
    while(!(*ctx_vwmterm->shutdown));

    vwmterm_data = (vwmterm_data_t *)ctx_vwmterm->anything;
    vterm_destroy(vwmterm_data->vterm);

    PT_DONE;
}
