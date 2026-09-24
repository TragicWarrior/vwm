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
#include "manage_windows.h"
#include "screensaver.h"
#include "winman.h"
#include "bkgd.h"

/* Has BUTTON1_PRESSED landed inside the open dropdown since it opened?
   The menubar opens the dropdown on its OWN press, and the matching
   release of that press lands at the mouse's current position -- which,
   over SSH where the latency between press and release is enough that
   the user has already moved off the menubar, is some random item in
   the just-opened dropdown.  Without this flag the dropdown would
   interpret that release as a click and exec the wrong item.

   Reset on open_dropdown / close_dropdown so we never carry state
   across two separate dropdown lifecycles. */
static bool g_dropdown_press_armed = false;

/*
    Apps menu categories.  The Apps dropdown lists one row per category
    that has visible apps, each marked as a submenu; the highlighted
    category's apps open in a second window beside that row (g_sub).
    Moving onto a category -- by key or by mouse -- opens its submenu;
    Right / Enter (or a click) moves the focus into it, Left / Esc brings
    the focus back.  g_cat_types[] maps a top-level row to its category.
*/
static vk_window_t  *g_sub = NULL;
static int          g_sub_row = -1;         /* top-level row it belongs to */
static bool         g_sub_focus = false;    /* keys go to the submenu */
static int          g_cat_types[VWM_MOD_TYPE_MAX];
static int          g_cat_count = 0;

static void apps_submenu_close(void);
static void apps_submenu_sync(vwm_t *vwm);
static void apps_submenu_focus(vwm_t *vwm, bool focus);

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
    if(scroll_y) *scroll_y = vk_listbox_get_scroll_pos(lb);
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

/* the apps of one category, styled like the Apps dropdown */
static vk_window_t*
create_category_menu(vwm_t *vwm, int type)
{
    vk_listbox_t    *listbox;
    vk_window_t     *window;
    vwm_module_t    *vwm_module = NULL;
    char            buf[NAME_MAX];
    int             max_width = 0;
    int             max_height = 0;
    int             scr_width, scr_height;
    bool            scroll;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    listbox = vk_listbox_create(8, 10);
    vk_widget_set_colors(VK_WIDGET(listbox), COLOR_WHITE, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(listbox), A_BOLD);
    vk_listbox_set_highlight(listbox, COLOR_WHITE, COLOR_BLACK);
    vk_listbox_set_highlight_attrs(listbox, A_BOLD);
    vk_listbox_set_unfocused(listbox, COLOR_WHITE, COLOR_CYAN);
    vk_listbox_set_wrap(listbox, FALSE);
    vk_object_set_kmio(VK_OBJECT(listbox), vwm_dropdown_kmio);

    do
    {
        vwm_module = vwm_module_find_by_type(vwm_module, type);
        if(vwm_module == NULL) break;

        if(vwm_module_get_zone(vwm_module) == MODULE_ZONE_CORE) continue;
        if(vwm_module_is_hidden(vwm_module)) continue;

        vwm_module_get_title(vwm_module, buf, sizeof(buf) - 1);
        vk_listbox_add_item(listbox, buf, vwm_menu_helper, vwm_module);
    }
    while(vwm_module != NULL);

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 4;
    scroll = max_height > scr_height;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    window = vk_window_create(max_width + 2, max_height + 2);
    vk_window_set_border_style(window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox), VK_INHERIT_NONE);

    if(scroll)
    {
        vk_scroller_t *scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
        vk_scroller_set_border_style(scroller, VK_BORDER_SINGLE);
        vk_scroller_set_border_colors(scroller, COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(scroller), A_BOLD);
        vk_scroller_set_scroll_source(scroller, VK_WIDGET(listbox));
        vk_scroller_set_scroll_info(scroller, vwm_menu_scroll_info);
        vk_scroller_set_scroll_apply(scroller, vk_listbox_scroll_apply);
        vk_widget_attach_scroller(VK_WIDGET(listbox), scroller);
    }

    return window;
}

static void
apps_submenu_close(void)
{
    vwm_t           *vwm;
    vk_listbox_t    *listbox;

    if(g_sub == NULL) return;

    vwm = vwm_get_instance();
    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(g_sub));

    listbox = VK_LISTBOX(vk_window_get_child(g_sub));
    vk_window_set_child(g_sub, NULL, VK_INHERIT_NONE);
    vk_listbox_destroy(listbox);
    vk_window_destroy(g_sub);

    g_sub = NULL;
    g_sub_row = -1;
    g_sub_focus = false;
}

/*
    Show the submenu of the Apps row under the highlight, beside that row:
    to the right of the dropdown, or to its left when it would run off
    the screen, and moved up when it would run off the bottom.
*/
static void
apps_submenu_sync(vwm_t *vwm)
{
    vk_listbox_t    *top;
    int             row, mx, my, mw, mh, sw, sh, x, y;
    int             scr_w, scr_h;

    if(vwm->menu == NULL || vwm->menu_item_idx != 0) return;

    top = VK_LISTBOX(vk_window_get_child(vwm->menu));
    row = vk_listbox_get_curr(top);

    if(row == g_sub_row && g_sub != NULL) return;

    apps_submenu_close();

    if(row < 0 || row >= g_cat_count) return;
    if(!vk_listbox_item_has_submenu(top, row)) return;

    g_sub = create_category_menu(vwm, g_cat_types[row]);
    g_sub_row = row;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
    vk_widget_get_position(VK_WIDGET(vwm->menu), &mx, &my);
    vk_widget_get_metrics(VK_WIDGET(vwm->menu), &mw, &mh);
    vk_widget_get_metrics(VK_WIDGET(g_sub), &sw, &sh);

    /* right of the dropdown; else left of it; else as far right as the
       screen allows (overlapping the dropdown rather than hiding it) */
    x = mx + mw;
    if(x + sw > scr_w) x = mx - sw;
    if(x < 0) x = scr_w - sw;
    if(x < 0) x = 0;

    /* the submenu's first item lines up with the category row */
    y = my + (row - vk_listbox_get_scroll_pos(top));
    if(y + sh > scr_h) y = scr_h - sh;
    if(y < 1) y = 1;

    vk_widget_move(VK_WIDGET(g_sub), x, y);
    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(g_sub));

    apps_submenu_focus(vwm, false);
}

/* Move the keyboard focus into the submenu (true) or back to the Apps
   list (false); the unfocused list keeps its row in a dim highlight. */
static void
apps_submenu_focus(vwm_t *vwm, bool focus)
{
    vk_listbox_t    *sub;

    if(g_sub == NULL) focus = false;
    g_sub_focus = focus;

    if(vwm->menu != NULL)
    {
        vk_listbox_t *top = VK_LISTBOX(vk_window_get_child(vwm->menu));

        vk_listbox_set_focused(top, !focus);
        vk_listbox_update(top);
        vk_window_update(vwm->menu);
    }

    if(g_sub == NULL) return;

    sub = VK_LISTBOX(vk_window_get_child(g_sub));
    vk_listbox_set_focused(sub, focus);
    vk_listbox_update(sub);
    vk_window_update(g_sub);
}

vk_window_t*
vwm_menubar_get_submenu(void)
{
    return g_sub;
}

/* after a terminal resize: close the submenu and reopen it beside the
   highlighted category (the dropdown itself was resized by the caller) */
void
vwm_menubar_refresh_submenu(void)
{
    vwm_t   *vwm = vwm_get_instance();
    bool    focus = g_sub_focus;

    if(g_sub == NULL) return;
    apps_submenu_close();
    apps_submenu_sync(vwm);
    apps_submenu_focus(vwm, focus);
}

static int
vwm_menubar_on_select(vk_object_t *object, int event, void *anything)
{
    vwm_t   *vwm;

    (void)object;
    (void)event;
    (void)anything;

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
vwm_teleport(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_teleport_prompt_show();

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
    open_dropdown(vwm, 1);

    return 0;
}

static int
vwm_apps_menu_activate(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;

    (void)widget;
    (void)anything;

    vwm = vwm_get_instance();
    open_dropdown(vwm, 0);

    return 0;
}

static int
vwm_minimized_menu_activate(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;

    (void)widget;
    (void)anything;

    vwm = vwm_get_instance();
    open_dropdown(vwm, 2);

    return 0;
}

static int
vwm_capture_screenshot(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_screenshot_open();
    return 0;
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
    vk_listbox_add_item(listbox, "Manage desktop",
        vwm_manage_windows_open, NULL);
    vk_listbox_add_item(listbox, "Switch desktop (Alt d)",
        vwm_switch_desktop, NULL);
    vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(listbox, "Lock screen",
        vwm_lock_screen, NULL);
    vk_listbox_add_item(listbox, "Capture screenshot",
        vwm_capture_screenshot, NULL);
    vk_listbox_add_item(listbox, "Print file",
        vwm_print_file, NULL);
    vk_listbox_add_item(listbox, "Teleport",
        vwm_teleport, NULL);
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
    vk_window_set_border_style(window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox), VK_INHERIT_NONE);

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

    /* one row per category that has at least one visible app */
    g_cat_count = 0;
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
        }
        while(vwm_module != NULL && category_found == FALSE);

        if(category_found == FALSE) continue;

        snprintf(buf, sizeof(buf), "%s", vwm_module_type_string(i));
        vk_listbox_add_item(listbox, buf, NULL, NULL);
        vk_listbox_set_item_submenu(listbox, g_cat_count, true);
        g_cat_types[g_cat_count++] = i;
    }
    vk_listbox_set_unfocused(listbox, COLOR_WHITE, COLOR_BLUE);

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 4;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    window = vk_window_create(max_width + 2, max_height + 2);
    vk_window_set_title(window, " Apps ");
    vk_window_set_border_style(window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox), VK_INHERIT_NONE);

    {
        vk_scroller_t *scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
        vk_scroller_set_border_style(scroller, VK_BORDER_SINGLE);
        vk_scroller_set_border_colors(scroller, COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(scroller), A_BOLD);
        vk_scroller_set_scroll_source(scroller, VK_WIDGET(listbox));
        vk_scroller_set_scroll_info(scroller, vwm_menu_scroll_info);
        vk_scroller_set_scroll_apply(scroller, vk_listbox_scroll_apply);
        vk_widget_attach_scroller(VK_WIDGET(listbox), scroller);
    }

    return window;
}

/*
    Window-menu item callback: raise the chosen window to the top of its deck,
    unhiding it first if it was minimized.  vwm_restore_window() does both (a
    show on an already-visible window is a no-op), so this one handler serves
    visible and minimized rows alike.
*/
static int
vwm_restore_minimized(vk_widget_t *widget, void *anything)
{
    (void)widget;

    if(anything != NULL) vwm_restore_window(VK_WIDGET(anything));

    return 0;
}

static vk_window_t*
create_windows_dropdown(vwm_t *vwm)
{
    vk_listbox_t    *listbox;
    vk_window_t     *window;
    vk_widget_t     *w;
    const char      *title;
    const char      *mark;
    const char      *caption = " Windows ";
    char            buf[NAME_MAX];
    int             max_width = 0;
    int             max_height = 0;
    int             scr_width, scr_height;
    int             count, i;
    int             shown = 0;

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

    /* List every window on the current desktop's deck -- visible and
       minimized alike.  Selecting one raises it to the top (and unhides it if
       it was minimized).  Minimized (hidden) windows carry a leading
       down-arrow marker; visible windows get a blank slot in its place so the
       titles stay column-aligned.  (A VDK listbox paints every row with one
       set of attributes, so hidden rows are flagged with a glyph rather than a
       dimmer color -- this matches the marker the Manage-windows tool uses.) */
    count = vk_deck_count(vwm->deck);
    for(i = 0; i < count; i++)
    {
        w = vk_deck_get_widget(vwm->deck, i);
        if(w == NULL) continue;

        if(vk_widget_get_state(w) & VK_STATE_VISIBLE)
            mark = " ";
        else
            mark = vwm_has_utf8() ? "\xe2\x86\x93" : "v";   /* U+2193 down arrow */

        title = vk_window_get_title(VK_WINDOW(w));
        if(title == NULL || title[0] == '\0') title = "(untitled)";

        snprintf(buf, sizeof(buf), "%s %s", mark, title);
        vk_listbox_add_item(listbox, buf, vwm_restore_minimized, w);
        shown++;
    }

    if(shown == 0)
        vk_listbox_add_item(listbox, "(none)", NULL, NULL);

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 4;
    /* vk_window centers the caption and truncates it to (window width - 4);
       window width is max_width + 2, so the usable title span is max_width - 2.
       Floor to strlen(caption) + 2 or a short app list (e.g. "htop") clips the
       caption's trailing space against the right frame. */
    if(max_width < (int)strlen(caption) + 2)
        max_width = (int)strlen(caption) + 2;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    window = vk_window_create(max_width + 2, max_height + 2);
    vk_window_set_title(window, caption);
    vk_window_set_border_style(window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(window, A_BOLD);
    vk_window_set_child(window, VK_WIDGET(listbox), VK_INHERIT_NONE);

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
        window = create_apps_dropdown(vwm);
    else if(idx == 2)
        window = create_windows_dropdown(vwm);
    else
        window = create_file_dropdown(vwm);

    vk_widget_get_position(VK_WIDGET(vwm->menubar), &menubar_x, &menubar_y);
    vk_menubar_get_item_position(vwm->menubar, idx, &item_x);

    vk_widget_move(VK_WIDGET(window), menubar_x + item_x, 1);
    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(window));

    vk_listbox_update(VK_LISTBOX(vk_window_get_child(window)));
    vk_window_update(window);

    vwm->menu = window;
    vwm->menu_item_idx = idx;

    /* a brand-new dropdown has seen no presses yet */
    g_dropdown_press_armed = false;

    /* the highlighted category shows its apps right away */
    if(idx == 0) apps_submenu_sync(vwm);
}

/* Mouse inside the Apps submenu: hover moves its highlight (and the
   focus); a click runs the app and closes both menus. */
static int
apps_submenu_mouse(vwm_t *vwm, MEVENT *mouse_event)
{
    vk_listbox_t    *listbox = VK_LISTBOX(vk_window_get_child(g_sub));
    int             beg_x, beg_y, row;
    mmask_t         bs = mouse_event->bstate;
    bool            was_armed;

    vk_widget_get_position(VK_WIDGET(g_sub), &beg_x, &beg_y);
    row = (mouse_event->y - beg_y - 1) + vk_listbox_get_scroll_pos(listbox);

    if(bs & (BUTTON1_CLICKED | BUTTON1_RELEASED))
    {
        was_armed = g_dropdown_press_armed;
        g_dropdown_press_armed = false;

        if(!(bs & BUTTON1_CLICKED) && !was_armed) return 0;

        if(row >= 0 && row < vk_listbox_get_item_count(listbox))
        {
            vk_listbox_set_curr(listbox, row);
            vk_listbox_exec_curr(listbox);
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
        }
        return 0;
    }

    if(bs & (BUTTON4_PRESSED | BUTTON5_PRESSED))
    {
        vk_scroller_t *scr = vk_widget_get_vscroller(VK_WIDGET(listbox));

        if(scr != NULL &&
           vk_scroller_nudge(scr, (bs & BUTTON4_PRESSED) ? -1 : 1, 0) == 0)
        {
            vk_listbox_update(listbox);
            vk_window_update(g_sub);
        }
        return 0;
    }

    if((bs & REPORT_MOUSE_POSITION) || (bs & BUTTON1_PRESSED))
    {
        if(bs & BUTTON1_PRESSED) g_dropdown_press_armed = true;

        if(row >= 0 && row < vk_listbox_get_item_count(listbox))
        {
            vk_listbox_set_curr(listbox, row);
            apps_submenu_focus(vwm, true);
        }
    }

    return 0;
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
    mmask_t         bs;
    bool            inside;
    bool            was_armed;

    vwm = vwm_get_instance();
    menu = vwm->menu;
    if(menu == NULL) return -1;

    /* the Apps submenu sits beside (and may overlap) the dropdown */
    if(g_sub != NULL)
    {
        int sx, sy, sw, sh;

        vk_widget_get_position(VK_WIDGET(g_sub), &sx, &sy);
        vk_widget_get_metrics(VK_WIDGET(g_sub), &sw, &sh);
        if(mouse_event->x >= sx && mouse_event->x < sx + sw &&
           mouse_event->y >= sy && mouse_event->y < sy + sh)
            return apps_submenu_mouse(vwm, mouse_event);
    }

    vk_widget_get_position(VK_WIDGET(menu), &beg_x, &beg_y);
    vk_widget_get_metrics(VK_WIDGET(menu), &w, &h);

    bs = mouse_event->bstate;
    inside = !(mouse_event->y < beg_y || mouse_event->y >= beg_y + h
        || mouse_event->x < beg_x || mouse_event->x >= beg_x + w);

    /* Any release (CLICKED is press+release atomic; RELEASED is the
       trailing half of a long press) ends the press sequence.  Capture
       the arm-state and clear it before we branch -- this guarantees
       the flag never lingers past the release that's supposed to
       resolve it, regardless of where the release lands. */
    if(bs & (BUTTON1_CLICKED | BUTTON1_RELEASED))
    {
        was_armed = g_dropdown_press_armed;
        g_dropdown_press_armed = false;

        if(!inside) return -1;

        /* CLICKED is atomic -- ncurses synthesizes it only when press
           and release happened at the same position within
           mouseinterval, so we trust it on its own.  RELEASED only
           counts as a click when the dropdown also saw the matching
           press (the menubar's release would arrive here unarmed). */
        if(!(bs & BUTTON1_CLICKED) && !was_armed) return 0;

        listbox = VK_LISTBOX(vk_window_get_child(menu));
        row = (mouse_event->y - beg_y - 1)
            + vk_listbox_get_scroll_pos(listbox);

        if(row >= 0 && row < vk_listbox_get_item_count(listbox)
            && vk_listbox_item_has_submenu(listbox, row))
        {
            /* a category: go into its submenu */
            vk_listbox_set_curr(listbox, row);
            apps_submenu_sync(vwm);
            apps_submenu_focus(vwm, true);
            return 0;
        }

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

    if(!inside) return -1;

    listbox = VK_LISTBOX(vk_window_get_child(menu));
    row = (mouse_event->y - beg_y - 1) + vk_listbox_get_scroll_pos(listbox);

    /* Wheel scroll — push a nudge on the scroller so the thumb
       drives the view instead of our hand-rolled set_scroll_pos.
       The Apps menu attaches a real scroller; VWM / Minimized menus
       may not, in which case fall back to pan the listbox view. */
    if(bs & (BUTTON4_PRESSED | BUTTON5_PRESSED))
    {
        vk_scroller_t *scr = vk_widget_get_vscroller(VK_WIDGET(listbox));
        if(scr != NULL)
        {
            int dy = (bs & BUTTON4_PRESSED) ? -1 : 1;
            if(vk_scroller_nudge(scr, dy, 0) == 0)
            {
                vk_listbox_update(listbox);
                vk_window_update(menu);
            }
            return 0;
        }
        /* Fallback for menus without an attached scroller (VWM / Minimized):
           keep the existing view-pan semantics. */
        int pos = vk_listbox_get_scroll_pos(listbox);
        if(bs & BUTTON4_PRESSED)    /* scroll up */
            pos--;
        else                        /* scroll down */
            pos++;
        vk_listbox_set_scroll_pos(listbox, pos);
        vk_listbox_update(listbox);
        vk_window_update(menu);
        return 0;
    }

    if((bs & REPORT_MOUSE_POSITION) || (bs & BUTTON1_PRESSED))
    {
        if(bs & BUTTON1_PRESSED) g_dropdown_press_armed = true;

        if(row >= 0 && row < vk_listbox_get_item_count(listbox)
            && !vk_listbox_item_is_separator(listbox, row))
        {
            vk_listbox_set_curr(listbox, row);
            /* hover by mouse: the category shows its apps */
            if(vwm->menu_item_idx == 0)
            {
                apps_submenu_sync(vwm);
                apps_submenu_focus(vwm, false);
            }
            vk_listbox_update(listbox);
            vk_window_update(menu);
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

    vk_menubar_add_item(vwm->menubar, "Apps",
        vwm_apps_menu_activate, NULL);
    vk_menubar_add_item(vwm->menubar, "VWM",
        vwm_file_menu_activate, NULL);
    vk_menubar_add_item(vwm->menubar, "(0) Windows",
        vwm_minimized_menu_activate, NULL);

    vk_object_register_event(VK_OBJECT(vwm->menubar),
        VK_EVENT_ON_SELECT, vwm_menubar_on_select, NULL);

    // " Apps " + "|" + " VWM " + "|" + " (99) Windows " = 6+1+5+1+14 = 27
    menubar_width = 27;
    vk_widget_resize(VK_WIDGET(vwm->menubar), menubar_width, 1);

    vk_menubar_update(vwm->menubar);

    {
        VWM_PANEL *panel = vwm_panel_get_data();
        vk_box_update(panel->box);
        vk_widget_draw(VK_WIDGET(panel->box));
    }

    vwm->menu_item_idx = -1;
}

/*
    Recount the windows on the current desktop's deck and update the
    "(N) Windows" menu-bar item, then repaint the panel row.  Called whenever
    the deck's membership can change -- a window opening, closing, or moving
    between desktops.  Minimizing/restoring leaves membership unchanged but
    refreshes through here too (cheap, and keeps the count correct if a hidden
    window is closed).
*/
void
vwm_window_menu_refresh(void)
{
    vwm_t       *vwm;
    vk_widget_t *w;
    char        label[32];
    int         count, i, n = 0;

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->menubar == NULL) return;

    count = vk_deck_count(vwm->deck);
    for(i = 0; i < count; i++)
    {
        w = vk_deck_get_widget(vwm->deck, i);
        if(w == NULL) continue;
        n++;
    }

    snprintf(label, sizeof(label), "(%d) Windows", n);
    vk_menubar_set_item_label(vwm->menubar, 2, label);

    {
        VWM_PANEL *panel = vwm_panel_get_data();
        vk_box_update(panel->box);
        vk_widget_draw(VK_WIDGET(panel->box));
    }
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

    apps_submenu_close();

    if(menu == NULL) return;

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(menu));

    listbox = VK_LISTBOX(vk_window_get_child(menu));

    /* detach the child while both are still valid: otherwise the window
       dtor list_del()s a freed listbox node and corrupts the heap */
    vk_window_set_child(menu, NULL, VK_INHERIT_NONE);

    vk_listbox_destroy(listbox);
    vk_window_destroy(menu);

    vwm->menu = NULL;
    vwm->menu_item_idx = -1;

    g_dropdown_press_armed = false;
}

int
vwm_menubar_ON_KEYSTROKE(int32_t keystroke)
{
    vwm_t           *vwm;
    vk_window_t     *menu;
    int             retval;

    vwm = vwm_get_instance();
    menu = vwm->menu;

    if(menu != NULL && g_sub != NULL && g_sub_focus)
    {
        vk_listbox_t *sub = VK_LISTBOX(vk_window_get_child(g_sub));

        if(keystroke == vwm->hotkey_menu)
        {
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        /* back to the category list; the submenu stays open */
        if(keystroke == 27 || keystroke == KEY_LEFT)
        {
            apps_submenu_focus(vwm, false);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_RIGHT) return KMIO_HANDLED;

        retval = vk_object_push_keystroke(VK_OBJECT(sub), keystroke);

        if(keystroke == KEY_CRLF && retval == 0)
        {
            vwm_menubar_close_dropdown();
            vk_menubar_set_focused(vwm->menubar, false);
            vk_menubar_update(vwm->menubar);
            return KMIO_HANDLED;
        }

        if(retval == 0)
        {
            vk_window_update(g_sub);
            return KMIO_HANDLED;
        }

        return keystroke;
    }

    if(menu != NULL)
    {
        vk_listbox_t *top = VK_LISTBOX(vk_window_get_child(menu));
        int          cur = vk_listbox_get_curr(top);

        /* on a category: Right / Enter go into its submenu */
        if(vwm->menu_item_idx == 0 && g_sub != NULL &&
           vk_listbox_item_has_submenu(top, cur) &&
           (keystroke == KEY_RIGHT || keystroke == KEY_CRLF))
        {
            apps_submenu_focus(vwm, true);
            return KMIO_HANDLED;
        }

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
            /* hover by key: the new category shows its apps */
            apps_submenu_sync(vwm);
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
