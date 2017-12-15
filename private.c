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

#include <string.h>
#include <inttypes.h>

#include <viper.h>

#include "vwm.h"
#include "modules.h"
#include "mainmenu.h"
#include "private.h"
#include "hotkeys.h"
#include "bkgd.h"

int
vwm_default_border_agent_focus(vwnd_t *vwnd, void *anything)
{
	const char		*title;
	uint32_t		window_state;
 	int		   	    y, x;

    (void)anything;

	title = viper_window_get_title(vwnd);

    window_decorate(WINDOW_FRAME(vwnd), (char*)title,TRUE);
    getmaxyx(WINDOW_FRAME(vwnd), y, x);
	mvwprintw(WINDOW_FRAME(vwnd), 0, x - sizeof("[X]") + 1,"[X]");

	window_state = viper_window_get_state(vwnd);
    if(window_state & STATE_NORESIZE)
        mvwaddch(WINDOW_FRAME(vwnd), y - 1, x - 1, '*');

    window_modify_border(WINDOW_FRAME(vwnd), A_BOLD,
        viper_color_pair(COLOR_WHITE, COLOR_MAGENTA));

    return 0;
}

int
vwm_default_border_agent_unfocus(vwnd_t *vwnd, void *anything)
{
	const char	    *title;
	uint32_t	    window_state;
 	int		        y, x;

    (void)anything;

	title = viper_window_get_title(vwnd);

    window_decorate(WINDOW_FRAME(vwnd), (char*)title, TRUE);
    getmaxyx(WINDOW_FRAME(vwnd), y, x);
	mvwprintw(WINDOW_FRAME(vwnd), 0, x - sizeof("[X]") + 1, "[X]");

	window_state = viper_window_get_state(vwnd);
    if(window_state & STATE_NORESIZE)
        mvwaddch(WINDOW_FRAME(vwnd), y - 1, x - 1, '*');

    window_modify_border(WINDOW_FRAME(vwnd), A_NORMAL,
	    viper_color_pair(COLOR_BLACK,COLOR_CYAN));

    return 0;
}

void
vwm_modules_preload(void)
{
    char            *module_dirs[] = { NULL, _VWM_SHARED_MODULES };
    char            *error_msg;
    int             array_sz;
    int             i;

    array_sz = sizeof(module_dirs) / sizeof(module_dirs[0]);
    module_dirs[0] = VWM_MOD_DIR;

    for(i = 0;i < array_sz;i++)
    {
        error_msg = vwm_modules_load(module_dirs[i]);

        if(error_msg != NULL)
        {

            endwin();
            printf("[EE] Module loading failed\n\r");
            printf("%s\n\r", error_msg);
            exit(0);
        }
    }

	return;
}

int
vwm_exit(vk_widget_t *widget, void *anything)
{
    extern int  shutdown;

    (void)widget;
    (void)anything;

    shutdown = 1;

    return 0;
}

int
vwm_toggle_winman(vk_widget_t *widget, void *anything)
{
    vwm_kmio_dispatch_hook_enter(VWM_HOTKEY_WM);

    return 0;
}
