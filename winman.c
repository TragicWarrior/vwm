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

#include <vdk.h>

#include "vwm.h"
#include "winman.h"
#include "mainmenu.h"
#include "bkgd.h"
#include "private.h"
#include "panel.h"


void
vwm_default_VWM_START(void)
{
    vwm_t   *vwm;

    vwm = vwm_get_instance();

    vwm_menubar_close_dropdown();
    vk_menubar_set_focused(vwm->menubar, false);

    vwm_panel_set_status(VWM_WM_HELP);

    vk_screen_set_wallpaper(vwm->screen, vwm_bkgd_simple_winman);

    vk_screen_refresh(vwm->screen);
    flash();
}

void
vwm_default_VWM_STOP(void)
{
    vwm_t       *vwm;
    vk_widget_t *top;

    vwm = vwm_get_instance();

    top = vk_deck_get_top(vwm->deck);
    if(top != NULL)
        vwm_panel_set_status(VWM_WINDOW_HELP);
    else
        vwm_panel_set_status("Press Alt ~ for Menu");

    vk_screen_set_wallpaper(vwm->screen, vwm_bkgd_simple_normal);

    vk_screen_refresh(vwm->screen);
    flash();
}

void
vwm_default_WINDOW_CLOSE(vk_widget_t *widget)
{
    vwm_t       *vwm;
    vk_widget_t *new_top;

    if(widget == NULL) return;

    vwm = vwm_get_instance();

    vk_object_emit(VK_OBJECT(widget), VWM_EVENT_ON_CLOSE);
    vk_deck_remove_widget(vwm->deck, widget);
    vk_widget_destroy(widget);

    new_top = vk_deck_get_top(vwm->deck);
    if(new_top != NULL)
        vk_window_update(VK_WINDOW(new_top));
    else
        vwm_panel_set_status("Press Alt ~ for Menu");

    vk_screen_refresh(vwm->screen);
}

void
vwm_default_WINDOW_CYCLE(void)
{
    vwm_t       *vwm;
    vk_widget_t *old_top;
    vk_widget_t *new_top;

    vwm = vwm_get_instance();

    old_top = vk_deck_get_top(vwm->deck);
    vk_deck_cycle(vwm->deck, VK_VECTOR_LEFT);
    new_top = vk_deck_get_top(vwm->deck);

    if(old_top != NULL) vk_window_update(VK_WINDOW(old_top));
    if(new_top != NULL) vk_window_update(VK_WINDOW(new_top));

    vk_screen_refresh(vwm->screen);
}

void
vwm_default_WINDOW_MOVE_UP(vk_widget_t *widget)
{
    int     x, y;

    if(widget == NULL) return;

    vk_widget_get_position(widget, &x, &y);
    vk_widget_move(widget, x, y - 1);
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_MOVE_DOWN(vk_widget_t *widget)
{
    int     x, y;

    if(widget == NULL) return;

    vk_widget_get_position(widget, &x, &y);
    vk_widget_move(widget, x, y + 1);
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_MOVE_LEFT(vk_widget_t *widget)
{
    int     x, y;

    if(widget == NULL) return;

    vk_widget_get_position(widget, &x, &y);
    vk_widget_move(widget, x - 1, y);
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_MOVE_RIGHT(vk_widget_t *widget)
{
    int     x, y;

    if(widget == NULL) return;

    vk_widget_get_position(widget, &x, &y);
    vk_widget_move(widget, x + 1, y);
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_INCREASE_HEIGHT(vk_widget_t *widget)
{
    int     width, height;

    if(widget == NULL) return;

    vk_widget_get_metrics(widget, &width, &height);
    vk_widget_resize(widget, width, height + 1);
    vk_window_update(VK_WINDOW(widget));
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_DECREASE_HEIGHT(vk_widget_t *widget)
{
    int     width, height;

    if(widget == NULL) return;

    vk_widget_get_metrics(widget, &width, &height);
    if(height <= 3) return;
    vk_widget_resize(widget, width, height - 1);
    vk_window_update(VK_WINDOW(widget));
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_INCREASE_WIDTH(vk_widget_t *widget)
{
    int     width, height;

    if(widget == NULL) return;

    vk_widget_get_metrics(widget, &width, &height);
    vk_widget_resize(widget, width + 1, height);
    vk_window_update(VK_WINDOW(widget));
    vk_screen_refresh(vwm_get_instance()->screen);
}

void
vwm_default_WINDOW_DECREASE_WIDTH(vk_widget_t *widget)
{
    int     width, height;

    if(widget == NULL) return;

    vk_widget_get_metrics(widget, &width, &height);
    if(width <= 3) return;
    vk_widget_resize(widget, width - 1, height);
    vk_window_update(VK_WINDOW(widget));
    vk_screen_refresh(vwm_get_instance()->screen);
}
