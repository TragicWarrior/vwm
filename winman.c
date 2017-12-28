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

#include <inttypes.h>

#include <viper.h>

#include "vwm.h"
#include "winman.h"
#include "mainmenu.h"
#include "bkgd.h"
#include "private.h"


void
vwm_default_VWM_START(vwnd_t *topmost_window)
{
    vwm_t       *vwm;
    uintmax_t   msg_id;
    int         screen_id;

    (void)topmost_window;

    vwm = vwm_get_instance();
    screen_id = CURRENT_SCREEN_ID;

    viper_screen_set_wallpaper(screen_id, vwm->wallpaper[screen_id],
            vwm_bkgd_simple_winman);

    msg_id = vwm_panel_message_find(VWM_MAIN_MENU_HELP);
    if(msg_id != 0) vwm_panel_message_del(msg_id);

    msg_id = vwm_panel_message_add(VWM_WM_HELP, -1);
    vwm_panel_message_promote(msg_id);

    viper_screen_redraw(screen_id, REDRAW_ALL | REDRAW_BACKGROUND);
    flash();

    return;
}

void
vwm_default_VWM_STOP(vwnd_t *topmost_window)
{
    vwm_t           *vwm;
    uintmax_t       msg_id;
    int             screen_id;

    (void)topmost_window;

	vwm = vwm_get_instance();
    screen_id = CURRENT_SCREEN_ID;

    viper_screen_set_wallpaper(screen_id, vwm->wallpaper[screen_id],
            vwm_bkgd_simple_normal);

    msg_id = vwm_panel_message_find(VWM_WM_HELP);
    if(msg_id != 0) vwm_panel_message_del(msg_id);

    msg_id = vwm_panel_message_add(VWM_MAIN_MENU_HELP, -1);
    vwm_panel_message_promote(msg_id);

	viper_screen_redraw(CURRENT_SCREEN_ID, REDRAW_ALL | REDRAW_BACKGROUND);
	flash();

	return;
}

void
vwm_default_WINDOW_CLOSE(vwnd_t *vwnd)
{
    (void)vwnd;

	return;
}

void
vwm_default_WINDOW_CYCLE(void)
{
	viper_deck_cycle(CURRENT_SCREEN_ID, TRUE, VECTOR_BOTTOM_TO_TOP);

	return;
}

void
vwm_default_WINDOW_MOVE_UP(vwnd_t *topmost_window)
{
	viper_mvwin_rel(topmost_window, 0, -1);

	return;
}

void
vwm_default_WINDOW_MOVE_DOWN(vwnd_t *topmost_window)
{
	viper_mvwin_rel(topmost_window, 0, 1);

	return;
}

void
vwm_default_WINDOW_MOVE_LEFT(vwnd_t *topmost_window)
{
	viper_mvwin_rel(topmost_window, -1, 0);

	return;
}

void
vwm_default_WINDOW_MOVE_RIGHT(vwnd_t *topmost_window)
{
	viper_mvwin_rel(topmost_window, 1, 0);

	return;
}

void
vwm_default_WINDOW_INCREASE_HEIGHT(vwnd_t *topmost_window)
{
    viper_wresize_rel(topmost_window, 0, 1);

    return;
}

void
vwm_default_WINDOW_DECREASE_HEIGHT(vwnd_t *topmost_window)
{
    int screen_id;

    screen_id = viper_window_get_screen_id(topmost_window);

    viper_wresize_rel(topmost_window, 0, -1);
    viper_screen_redraw(screen_id, REDRAW_ALL | REDRAW_BACKGROUND);

    return;
}

void
vwm_default_WINDOW_INCREASE_WIDTH(vwnd_t *topmost_window)
{
    viper_wresize_rel(topmost_window, 1, 0);

    return;
}

void
vwm_default_WINDOW_DECREASE_WIDTH(vwnd_t *topmost_window)
{
    int screen_id;

    viper_wresize_rel(topmost_window, -1, 0);

    screen_id = viper_window_get_screen_id(topmost_window);
    viper_screen_redraw(screen_id, REDRAW_ALL | REDRAW_BACKGROUND);

    return;
}

