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

#include "psthread.h"
#include "events.h"

int16_t
vwmterm_psthread(ps_task_t *task,void *anything)
{
    WINDOW      *window;
    vterm_t     *vterm;
    ssize_t     bytes_read;

    window = (WINDOW*)anything;

    // viper_thread_enter();
    vterm = viper_window_get_userptr(window);
    // viper_thread_leave();

    bytes_read = vterm_read_pipe(vterm);

    // handle no data condition
    if(bytes_read == 0) return PSTHREAD_CONTINUE;

    // handle pipe error condition
    if(bytes_read == -1)
    {
        // viper_thread_enter();
        viper_window_destroy(window);
        // viper_thread_leave();

        return PSTHREAD_TERMINATE;
    }

    if(bytes_read > 0)
    {
        // viper_thread_enter();
        vterm_wnd_update(vterm);
        viper_window_redraw(window);
        // viper_thread_leave();
    }

    return PSTHREAD_CONTINUE;
}



