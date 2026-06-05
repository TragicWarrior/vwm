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

#include <viper.h>

#include "vwm.h"
#include "mainmenu.h"
#include "modules.h"
#include "strings.h"
#include "private.h"
#include "events.h"

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
vwm_menu_kmio(vk_object_t *object, int32_t keystroke)
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

vwnd_t*
vwm_main_menu(void)
{
    vk_listbox_t    *listbox;
    vk_frame_t      *frame;
    vwnd_t          *vwnd;
	int			    width = 8, height = 10;

	vwm_module_t	*vwm_module;
    char            buf[NAME_MAX];

    int             max_width = 0;
    int             max_height = 0;
    int             scr_width;
    int             scr_height;
    bool            category_found;

    int             i;

    getmaxyx(CURRENT_SCREEN, scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    listbox = vk_listbox_create(width, height);
    vk_listbox_set_highlight(listbox, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_wrap(listbox, TRUE);
    vk_object_set_kmio(VK_OBJECT(listbox), vwm_menu_kmio);

    // iterate through the categories defined in modules.def
    for(i = 0; i < VWM_MOD_TYPE_MAX; i++)
    {
        vwm_module = NULL;
        category_found = FALSE;

        do
        {
            vwm_module = vwm_module_find_by_type(vwm_module, i);
            if(vwm_module == NULL) break;

            if(vwm_module_get_zone(vwm_module) == MODULE_ZONE_CORE) continue;

            category_found = TRUE;

            vwm_module_get_title(vwm_module, buf, sizeof(buf) - 1);

            vk_listbox_add_item(listbox, buf,
                vwm_menu_helper, vwm_module);
        }
        while(vwm_module != NULL);

        if(category_found == TRUE)
            vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    }

    vk_listbox_add_item(listbox, "Toggle WM (alt + w)",
        vwm_toggle_winman, NULL);
    vk_listbox_add_separator(listbox, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(listbox, "Exit", vwm_exit, NULL);

    vk_listbox_update(listbox);
    vk_listbox_get_metrics(listbox, &max_width, &max_height);
    max_width += 3;
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), max_width, max_height);

    frame = vk_frame_create(max_width + 2, max_height + 2);
    vk_frame_set_border_style(frame, VK_FRAME_SINGLE);
    vk_frame_set_child(frame, VK_WIDGET(listbox));

    {
        vk_scroller_t *scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
        vk_scroller_set_border_style(scroller, VK_FRAME_SINGLE);
        vk_scroller_set_border_colors(scroller, COLOR_RED, COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(scroller), A_BOLD);
        vk_scroller_set_scroll_source(scroller, VK_WIDGET(listbox));
        vk_scroller_set_scroll_info(scroller, vwm_menu_scroll_info);
        vk_widget_attach_scroller(VK_WIDGET(listbox), scroller);
    }

	vwnd = viper_window_create(CURRENT_SCREEN_ID, FALSE, " Menu ", 1, 1,
        max_width + 2, max_height + 2);

    vk_widget_set_surface(VK_WIDGET(frame), VWINDOW(vwnd));
    vk_widget_resize(VK_WIDGET(frame), max_width + 2, max_height + 2);

	viper_window_set_key_func(vwnd, vwm_main_menu_ON_KEYSTROKE);
	viper_window_set_userptr(vwnd, (void*)frame);

    viper_event_set(vwnd, "window-close", vwm_main_menu_ON_CLOSE, NULL);
    viper_event_set(vwnd, "term-resized",
        vwm_main_menu_ON_TERM_RESIZED, (void*)frame);

    vk_listbox_update(listbox);
    vk_frame_update(frame);
    vk_widget_draw(VK_WIDGET(frame));
    viper_window_set_focus(vwnd);
    viper_window_redraw(vwnd);

	return vwnd;
}

int
vwm_main_menu_hotkey(void)
{
	vwnd_t  *vwnd = NULL;

	vwnd = vwm_main_menu();
	viper_window_set_class(vwnd, vwm_main_menu);
	viper_window_set_top(vwnd);

	return 0;
}

int
vwm_main_menu_ON_CLOSE(vwnd_t *vwnd, void *anything)
{
    vk_frame_t      *frame;
    vk_listbox_t    *listbox;

    (void)anything;

    if(vwnd == NULL) return -1;

    frame = (vk_frame_t *)viper_window_get_userptr(vwnd);
    listbox = VK_LISTBOX(vk_frame_get_child(frame));

    vk_listbox_destroy(listbox);
    vk_frame_destroy(frame);

    return 0;
}


int
vwm_main_menu_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd)
{
    vwm_t           *vwm;
    vk_frame_t      *frame;
    int             retval;

    vwm = vwm_get_instance();

    frame = (vk_frame_t *)viper_window_get_userptr(vwnd);
    if(keystroke == -1) return -1;

    if(keystroke == vwm->hotkey_menu)
    {
        viper_window_close(vwnd);
        return 0;
    }

    retval = vk_object_push_keystroke(VK_OBJECT(frame), keystroke);

    if(keystroke == 10 && retval == 0)
    {
        viper_window_close(vwnd);
        return KMIO_HANDLED;
    }

    if(retval == 0)
    {
        vk_frame_update(frame);
        vk_widget_draw(VK_WIDGET(frame));
        viper_window_redraw(vwnd);
        return KMIO_HANDLED;
    }

	return keystroke;
}

