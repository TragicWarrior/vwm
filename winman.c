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

/*
    VK_EVENT_ON_FINALIZE handler, registered on every deck.  The deck's
    membership or stacking just changed, so repaint each member's decoration.
    A window's decorator recolors it from vk_deck_get_top(), so a plain
    vk_window_update() per member is all it takes to keep the focus borders
    correct -- no call site has to demote the old top and promote the new one
    by hand.  Decoration ONLY: this never calls vk_screen_refresh(); the
    operation that changed the deck owns the single refresh.
*/
int
vwm_on_deck_finalize(vk_object_t *object, int event, void *anything)
{
    vk_deck_t   *deck;
    int          i, n;

    (void)event;
    (void)anything;

    deck = VK_DECK(object);
    if(deck == NULL) return 0;

    n = vk_deck_count(deck);
    for(i = 0; i < n; i++)
    {
        vk_widget_t *w = vk_deck_get_widget(deck, i);
        if(w != NULL) vk_window_update(VK_WINDOW(w));
    }

    return 0;
}

void
vwm_default_WINDOW_CLOSE(vk_widget_t *widget)
{
    vwm_t   *vwm;

    if(widget == NULL) return;

    vwm = vwm_get_instance();

    vk_object_emit(VK_OBJECT(widget), VWM_EVENT_ON_CLOSE);

    /* removing the widget fires ON_FINALIZE, which re-decorates whatever
       window is now on top -- no manual focus hand-off needed here */
    vk_deck_remove_widget(vwm->deck, widget);
    vk_widget_destroy(widget);

    if(vk_deck_get_top(vwm->deck) == NULL)
        vwm_panel_set_status("Press Alt ~ for Menu");

    vwm_minimized_refresh();
    vk_screen_refresh(vwm->screen);
}

/*
    Minimize a window: hide it in place (the deck blitter skips non-visible
    members) and hand focus to the next visible window.  The window stays in
    the deck -- the "(N) Minimized" panel item and the manage-windows tool
    enumerate the hidden members and restore them.
*/
void
vwm_minimize_window(vk_widget_t *widget)
{
    vwm_t   *vwm;

    if(widget == NULL) return;

    vwm = vwm_get_instance();

    /* hiding a member isn't a deck mutation, so the deck can't fire
       ON_FINALIZE on its own -- trigger it so the newly-exposed top window
       picks up its focused decoration */
    vk_widget_hide(widget);
    vk_deck_finalize(vwm->deck);

    if(vk_deck_get_top(vwm->deck) == NULL)
        vwm_panel_set_status("Press Alt ~ for Menu");

    vwm_minimized_refresh();
    vk_screen_refresh(vwm->screen);
}

/*
    Restore a minimized window: show it again and raise it to the top of the
    deck (focus).
*/
void
vwm_restore_window(vk_widget_t *widget)
{
    vwm_t   *vwm;

    if(widget == NULL) return;

    vwm = vwm_get_instance();

    /* show, then raise: vk_deck_set_top() fires ON_FINALIZE, which repaints
       the restored window as focused and demotes the previous top */
    vk_widget_show(widget);
    vk_deck_set_top(vwm->deck, widget);

    vwm_minimized_refresh();
    vk_screen_refresh(vwm->screen);
}

void
vwm_default_WINDOW_CYCLE(void)
{
    vwm_t   *vwm;

    vwm = vwm_get_instance();

    /* cycling rotates the deck, firing ON_FINALIZE -- which repaints the
       window rotating out of focus and the one rotating into it */
    vk_deck_cycle(vwm->deck, VK_VECTOR_LEFT);

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
