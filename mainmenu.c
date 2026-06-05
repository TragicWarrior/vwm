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

#include <dirent.h>
#include <string.h>
#include <sys/types.h>

#include <vdk.h>

#include "vwm.h"
#include "mainmenu.h"
#include "modules.h"
#include "programs.h"
#include "strings.h"
#include "private.h"
#include "events.h"
#include "panel.h"
#include "manage_apps.h"
#include "manage_hotkeys.h"
#include "manage_settings.h"
#include "screensaver.h"

static void
vwm_menu_scroll_info(vk_widget_t *child,
    int *content_h, int *content_w,
    int *scroll_y, int *scroll_x)
{
    vk_listbox_t *lb = VK_LISTBOX(child);
    int metrics_w = 0;

    vk_listbox_get_metrics(lb, &metrics_w, NULL);

    if(content_h) *content_h = vk_listbox_get_item_count(lb);
    if(content_w) *content_w = metrics_w;
    if(scroll_y) *scroll_y = vk_listbox_get_curr(lb);
    if(scroll_x) *scroll_x = 0;
}

static int
vwm_dropdown_kmio(vk_object_t *object, int32_t keystroke)
{
    vk_listbox_t    *listbox = VK_LISTBOX(object);

    switch(keystroke)
    {
        case KEY_UP:
            vk_listbox_set_prev(listbox);
            break;

        case KEY_DOWN:
            vk_listbox_set_next(listbox);
            break;

        case KEY_CRLF:
            return vk_listbox_exec_curr(listbox);

        default:
            return -1;
    }

    vk_listbox_update(listbox);

    return 0;
}

static void
open_dropdown(vwm_t *vwm, int idx);

static int
vwm_menubar_on_select(vk_object_t *object, int event, void *data)
{
    vwm_t   *vwm;

    (void)object;
    (void)event;
    (void)data;

    vwm = vwm_get_instance();

    if(vwm->menu != NULL)
    {
        int new_idx = vk_menubar_get_curr(vwm->menubar);

        if(new_idx != vwm->menu_item_idx)
        {
            vwm_menubar_close_dropdown();
            open_dropdown(vwm, new_idx);
        }
    }

    return 0;
}

static int
vwm_switch_desktop(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_desktop_prompt_show();

    return 0;
}

static int
vwm_reload_apps(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_programs_reload();

    return 0;
}

static int
vwm_file_menu_activate(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;

    (void)widget;
    (void)anything;

    vwm = vwm_get_instance();
    open_dropdown(vwm, 0);

    return 0;
}

static int
vwm_apps_menu_activate(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;

    (void)widget;
    (void)anything;

    vwm = vwm_get_instance();
    open_dropdown(vwm, 1);

    return 0;
}

static int
vwm_capture_screenshot(vk_widget_t *widget, void *anything)
{
    vwm_module_t    *mod;

    (void)anything;

    mod = vwm_module_find_by_name("screen-capture");
    if(mod == NULL) return 0;

    return vwm_menu_helper(widget, mod);
}

static int
vwm_lock_screen(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_screensaver_activate();

    return 0;
}

static int
vwm_print_file(vk_widget_t *widget, void *anything)
{
    vwm_module_t    *mod;

    (void)anything;

    mod = vwm_module_find_by_name("print-file");
    if(mod == NULL) return 0;

    return vwm_menu_helper(widget, mod);
}

static vk_window_t*
create_file_dropdown(vwm_t *vwm)
{
    vk_listbox_t    *listbox;
    vk_window_t     *window;
    int             max_width = 0;
    int             max_height = 0;
    int             scr_width, scr_height;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    listbox = vk_listbox_create(8, 10);
    vk_widget_set_colors(VK_WIDGET(listbox), COLOR_WHITE, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(listbox), A_BOLD);
    vk_listbox_set_highlight(listbox, COLOR_WHITE, COLOR_BLACK);
    vk_listbox_set_highlight_attrs(listbox, A_BOLD);
    vk_listbox_set_wrap(listbox, TRUE);
    vk_object_set_kmio(VK_OBJECT(listbox), vwm_dropdown_kmio);

    vk_listbox_add_item(listbox, "Manage windows (Alt w)",
        vwm_toggle_winman, NULL);
    vk_listbox_add_item(listbox, "Switch desktop (Alt d)",
        vwm_switch_desktop, NULL);
    vk_listbox_add_item(listbox, "Lock screen",
        vwm_lock_screen, NULL);
    vk_listbox_add_item(listbox, "Capture screenshot",
        vwm_capture_screenshot, NULL);
    vk_listbox_add_item(listbox, "Print file",
        vwm_print_file, NULL);
    vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(listbox, "Manage Apps Menu",
        vwm_manage_apps_open, NULL);
    vk_listbox_add_item(listbox, "Reload Apps Menu",
        vwm_reload_apps, NULL);
    vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(listbox, "Manage Hotkeys",
        vwm_manage_hotkeys_open, NULL);
    vk_listbox_add_item(listbox, "Settings",
        vwm_manage_settings_open, NULL);
    vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(listbox, "Exit", vwm_exit, NULL);

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 4;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    window = vk_window_create(max_width + 2, max_height + 2);
    vk_window_set_title(window, " VWM ");
    vk_window_set_border_style(window, VK_FRAME_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox));

    return window;
}

static vk_window_t*
create_apps_dropdown(vwm_t *vwm)
{
    vk_listbox_t    *listbox;
    vk_window_t     *window;
    vwm_module_t    *vwm_module;
    char            buf[NAME_MAX];
    int             max_width = 0;
    int             max_height = 0;
    int             scr_width, scr_height;
    bool            category_found;
    int             i;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    listbox = vk_listbox_create(8, 10);
    vk_widget_set_colors(VK_WIDGET(listbox), COLOR_WHITE, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(listbox), A_BOLD);
    vk_listbox_set_highlight(listbox, COLOR_WHITE, COLOR_BLACK);
    vk_listbox_set_highlight_attrs(listbox, A_BOLD);
    vk_listbox_set_wrap(listbox, FALSE);
    vk_object_set_kmio(VK_OBJECT(listbox), vwm_dropdown_kmio);

    for(i = 0; i < VWM_MOD_TYPE_MAX; i++)
    {
        vwm_module = NULL;
        category_found = FALSE;

        do
        {
            vwm_module = vwm_module_find_by_type(vwm_module, i);
            if(vwm_module == NULL) break;

            if(vwm_module_get_zone(vwm_module) == MODULE_ZONE_CORE) continue;
            if(vwm_module_is_hidden(vwm_module)) continue;

            category_found = TRUE;

            vwm_module_get_title(vwm_module, buf, sizeof(buf) - 1);

            vk_listbox_add_item(listbox, buf,
                vwm_menu_helper, vwm_module);
        }
        while(vwm_module != NULL);

        if(category_found == TRUE)
            vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    }

    /* remove trailing separator */
    {
        int last = vk_listbox_get_item_count(listbox) - 1;
        if(last >= 0 && vk_listbox_item_is_separator(listbox, last))
            vk_listbox_remove_item(listbox, last);
    }

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 4;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    window = vk_window_create(max_width + 2, max_height + 2);
    vk_window_set_title(window, " Apps ");
    vk_window_set_border_style(window, VK_FRAME_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox));

    {
        vk_scroller_t *scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
        vk_scroller_set_border_style(scroller, VK_FRAME_SINGLE);
        vk_scroller_set_border_colors(scroller, COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(scroller), A_BOLD);
        vk_scroller_set_scroll_source(scroller, VK_WIDGET(listbox));
        vk_scroller_set_scroll_info(scroller, vwm_menu_scroll_info);
        vk_widget_attach_scroller(VK_WIDGET(listbox), scroller);
    }

    return window;
}

static void
open_dropdown(vwm_t *vwm, int idx)
{
    vk_window_t     *window;
    int             menubar_x, menubar_y;
    int             item_x;

    if(vwm->menu != NULL) return;

    if(idx == 0)
        window = create_file_dropdown(vwm);
    else
        window = create_apps_dropdown(vwm);

    vk_widget_get_position(VK_WIDGET(vwm->menubar), &menubar_x, &menubar_y);
    vk_menubar_get_item_position(vwm->menubar, idx, &item_x);

    vk_widget_move(VK_WIDGET(window), menubar_x + item_x, 1);
    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(window));

    vk_listbox_update(VK_LISTBOX(vk_window_get_child(window)));
    vk_window_update(window);

    vwm->menu = window;
    vwm->menu_item_idx = idx;
}

int
vwm_dropdown_mouse(MEVENT *mouse_event)
{
    vwm_t           *vwm;
    vk_window_t     *menu;
    vk_listbox_t    *listbox;
    int             beg_y, beg_x;
    int             w, h;
    int             row;

    vwm = vwm_get_instance();
    menu = vwm->menu;
    if(menu == NULL) return -1;

    vk_widget_get_position(VK_WIDGET(menu), &beg_x, &beg_y);
    vk_widget_get_metrics(VK_WIDGET(menu), &w, &h);

    if(mouse_event->y < beg_y || mouse_event->y >= beg_y + h
        || mouse_event->x < beg_x || mouse_event->x >= beg_x + w)
        return -1;

    listbox = VK_LISTBOX(vk_window_get_child(menu));
    row = (mouse_event->y - beg_y - 1) + vk_listbox_get_scroll_pos(listbox);

    if((mouse_event->bstate & REPORT_MOUSE_POSITION)
        || (mouse_event->bstate & BUTTON1_PRESSED))
    {
        if(row >= 0 && row < vk_listbox_get_item_count(listbox)
            && !vk_listbox_item_is_separator(listbox, row))
        {
            vk_listbox_set_curr(listbox, row);
            vk_listbox_update(listbox);
            vk_window_update(menu);
        }
        return 0;
    }

    if(mouse_event->bstate & BUTTON4_PRESSED)
    {
        vk_listbox_set_prev(listbox);
        vk_listbox_update(listbox);
        vk_window_update(menu);
        return 0;
    }

    if(mouse_event->bstate & BUTTON5_PRESSED)
    {
        vk_listbox_set_next(listbox);
        vk_listbox_update(listbox);
        vk_window_update(menu);
        return 0;
    }

    if(mouse_event->bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED))
    {
        if(row >= 0 && row < vk_listbox_get_item_count(listbox)
            && !vk_listbox_item_is_separator(listbox, row))
        {
            vk_listbox_set_curr(listbox, row);
            vk_listbox_exec_curr(listbox);
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
        }
        return 0;
    }

    return 0;
}

void
vwm_menubar_init(void)
{
    vwm_t       *vwm;
    int         menubar_width;

    vwm = vwm_get_instance();

    vk_menubar_add_item(vwm->menubar, "VWM",
        vwm_file_menu_activate, NULL);
    vk_menubar_add_item(vwm->menubar, "Apps",
        vwm_apps_menu_activate, NULL);

    vk_object_register_event(VK_OBJECT(vwm->menubar),
        VK_EVENT_ON_SELECT, vwm_menubar_on_select, NULL);

    // " VWM " + "|" + " Apps " = 5 + 1 + 6 = 12
    menubar_width = 12;
    vk_widget_resize(VK_WIDGET(vwm->menubar), menubar_width, 1);

    vk_menubar_update(vwm->menubar);

    {
        VWM_PANEL *panel = vwm_panel_get_data();
        vk_box_update(panel->box);
        vk_widget_draw(VK_WIDGET(panel->box));
    }

    vwm->menu_item_idx = -1;
}

int
vwm_menubar_hotkey(void)
{
    vwm_t   *vwm;

    vwm = vwm_get_instance();

    if(vwm->menu != NULL)
    {
        vwm_menubar_close_dropdown();
        vk_menubar_set_focused(vwm->menubar, false);
        vk_menubar_update(vwm->menubar);
        return 0;
    }

    if(vk_menubar_get_focused(vwm->menubar))
    {
        vk_menubar_set_focused(vwm->menubar, false);
        vk_menubar_update(vwm->menubar);
        return 0;
    }

    vk_menubar_set_curr(vwm->menubar, 0);
    vk_menubar_set_focused(vwm->menubar, true);
    vk_menubar_update(vwm->menubar);

    return 0;
}

void
vwm_menubar_close_dropdown(void)
{
    vwm_t           *vwm;
    vk_window_t     *menu;
    vk_listbox_t    *listbox;

    vwm = vwm_get_instance();
    menu = vwm->menu;

    if(menu == NULL) return;

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(menu));

    listbox = VK_LISTBOX(vk_window_get_child(menu));

    /* detach the child while both are still valid: otherwise the window
       dtor list_del()s a freed listbox node and corrupts the heap */
    vk_window_set_child(menu, NULL);

    vk_listbox_destroy(listbox);
    vk_window_destroy(menu);

    vwm->menu = NULL;
    vwm->menu_item_idx = -1;
}

int
vwm_menubar_ON_KEYSTROKE(int32_t keystroke)
{
    vwm_t           *vwm;
    vk_window_t     *menu;
    int             retval;

    vwm = vwm_get_instance();
    menu = vwm->menu;

    if(menu != NULL)
    {
        if(keystroke == vwm->hotkey_menu || keystroke == 27)
        {
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_LEFT)
        {
            vk_menubar_set_prev(vwm->menubar);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_RIGHT)
        {
            vk_menubar_set_next(vwm->menubar);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        retval = vk_object_push_keystroke(VK_OBJECT(menu), keystroke);

        if(keystroke == KEY_CRLF && retval == 0)
        {
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(retval == 0)
        {
            vk_window_update(menu);
            return KMIO_HANDLED;
        }

        return keystroke;
    }

    if(vk_menubar_get_focused(vwm->menubar))
    {
        if(keystroke == 27)
        {
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_LEFT)
        {
            vk_menubar_set_prev(vwm->menubar);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_RIGHT)
        {
            vk_menubar_set_next(vwm->menubar);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_CRLF || keystroke == ' ' || keystroke == KEY_DOWN)
        {
            vk_menubar_exec_curr(vwm->menubar);
            return KMIO_HANDLED;
        }

        return keystroke;
    }

    return keystroke;
}
