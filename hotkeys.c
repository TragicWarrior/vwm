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
#include "hotkeys.h"
#include "mainmenu.h"
#include "bkgd.h"
#include "private.h"

#define     KEY_PLUS            '+'
#define     KEY_CTRL_DOWN       525

#define     KEY_MINUS           '-'
#define     KEY_CTRL_UP         566

#define     KEY_GREATER_THAN    '>'
#define     KEY_CTRL_RIGHT      560

#define     KEY_LESS_THAN       '<'
#define     KEY_CTRL_LEFT       545

static void vwm_default_WINDOW_INCREASE_HEIGHT(vwnd_t *);
static void vwm_default_WINDOW_DECREASE_HEIGHT(vwnd_t *);
static void vwm_default_WINDOW_INCREASE_WIDTH(vwnd_t *);
static void vwm_default_WINDOW_DECREASE_WIDTH(vwnd_t *);


int32_t
vwm_kmio_dispatch_hook_enter(int32_t keystroke)
{
    vwm_t       *vwm;

    vwm = vwm_get_instance();

    if(keystroke == VWM_HOTKEY_WM)
    {
        vwm->state ^= VWM_STATE_ACTIVE;

        if(vwm->state & VWM_STATE_ACTIVE)
            vwm_default_VWM_START((void*)TOPMOST_MANAGED);
        else
            vwm_default_VWM_STOP((void*)TOPMOST_MANAGED);

        return -1;
    }

    if(vwm->state & VWM_STATE_ACTIVE)
    {
        switch(keystroke)
        {
            case 17:
                vwm_default_WINDOW_CLOSE(TOPMOST_MANAGED); return -1;
            case KEY_TAB:
                vwm_default_WINDOW_CYCLE(); return -1;
            case KEY_UP:
                vwm_default_WINDOW_MOVE_UP(TOPMOST_MANAGED); return -1;
            case KEY_DOWN:
                vwm_default_WINDOW_MOVE_DOWN(TOPMOST_MANAGED); return -1;
            case KEY_LEFT:
                vwm_default_WINDOW_MOVE_LEFT(TOPMOST_MANAGED); return -1;
            case KEY_RIGHT:
                vwm_default_WINDOW_MOVE_RIGHT(TOPMOST_MANAGED); return -1;

            case KEY_PLUS:
            case KEY_CTRL_DOWN:
                vwm_default_WINDOW_INCREASE_HEIGHT(TOPMOST_MANAGED); return -1;
            case KEY_MINUS:
            case KEY_CTRL_UP:
                vwm_default_WINDOW_DECREASE_HEIGHT(TOPMOST_MANAGED); return -1;
            case KEY_GREATER_THAN:
            case KEY_CTRL_RIGHT:
                vwm_default_WINDOW_INCREASE_WIDTH(TOPMOST_MANAGED); return -1;
            case KEY_LESS_THAN:
            case KEY_CTRL_LEFT:
                vwm_default_WINDOW_DECREASE_WIDTH(TOPMOST_MANAGED); return -1;

            default:
                // endwin();
                // printf("%d\n",keystroke);
                // exit(0);
                return keystroke;
        }
    }

    if(!(vwm->state & VWM_STATE_ACTIVE))
    {
        if(keystroke == vwm->hotkey_menu)
        {
            vwm_main_menu_hotkey();
            return -1;
        }
    }

    return keystroke;
}

void
vwm_default_VWM_START(vwnd_t *topmost_window)
{
    vwm_t       *vwm;
    // WINDOW      *wallpaper_wnd;
    uintmax_t   msg_id;
    int         screen_id;

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
    // WINDOW          *wallpaper_wnd;
    uintmax_t       msg_id;
    int             screen_id;

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

static void
vwm_default_WINDOW_INCREASE_HEIGHT(vwnd_t *topmost_window)
{
	viper_wresize_rel(topmost_window, 0, 1);

	return;
}

static void
vwm_default_WINDOW_DECREASE_HEIGHT(vwnd_t *topmost_window)
{
    int screen_id;

    screen_id = viper_window_get_screen_id(topmost_window);

	viper_wresize_rel(topmost_window, 0, -1);
    viper_screen_redraw(screen_id, REDRAW_ALL | REDRAW_BACKGROUND);

	return;
}

static void
vwm_default_WINDOW_INCREASE_WIDTH(vwnd_t *topmost_window)
{
	viper_wresize_rel(topmost_window, 1, 0);

	return;
}

static void
vwm_default_WINDOW_DECREASE_WIDTH(vwnd_t *topmost_window)
{
    int screen_id;

	viper_wresize_rel(topmost_window, -1, 0);

    screen_id = viper_window_get_screen_id(topmost_window);
    viper_screen_redraw(screen_id, REDRAW_ALL | REDRAW_BACKGROUND);

	return;
}


