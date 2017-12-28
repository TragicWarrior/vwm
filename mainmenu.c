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

vwnd_t*
vwm_main_menu(void)
{
    vk_menu_t       *menu;
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
    scr_height -= 4;

    menu = vk_menu_create(width, height);
    vk_menu_set_frame(menu, VK_FRAME_SINGLE);
    vk_listbox_set_highlight(VK_LISTBOX(menu), COLOR_BLACK, COLOR_RED);
    vk_listbox_set_wrap(VK_LISTBOX(menu), TRUE);

    // iterate through the categories defined in modules.def
    for(i = 0;i < VWM_MOD_TYPE_MAX;i++)
    {
        vwm_module = NULL;
        category_found = FALSE;

        do
        {
            vwm_module = vwm_module_find_by_type(vwm_module, i);
            if(vwm_module == NULL) break;

            category_found = TRUE;

            vwm_module_get_title(vwm_module, buf, sizeof(buf) - 1);

            vk_listbox_add_item(VK_LISTBOX(menu), buf,
                vwm_menu_helper, vwm_module);
        }
        while(vwm_module != NULL);

        // add a separator whenever a new category of module is found
        if(category_found == TRUE)
            vk_menu_add_separator(menu, VK_SEPARATOR_SINGLE);
    }

    // add some items manually
    vk_listbox_add_item(VK_LISTBOX(menu), "Toggle WM (alt + w)",
        vwm_toggle_winman, NULL);
    vk_menu_add_separator(menu, VK_SEPARATOR_SINGLE);
    vk_listbox_add_item(VK_LISTBOX(menu), "Exit", vwm_exit, NULL);

    vk_listbox_get_metrics(VK_LISTBOX(menu), &max_width, &max_height);
    if(max_width > scr_width) max_width = scr_width;
    if(max_height > scr_height) max_height = scr_height;

	vwnd = viper_window_create(CURRENT_SCREEN_ID, FALSE, " Menu ", 1, 1,
        max_width + 2, max_height + 2);
    /*
        todo:   it would be nice if the user could resize the menu
                (especially in the horizonal direction) and add more
                columns to the display.  right now, it's not a priority
                (but it would be easy to implement).  just need a few
                lines of code for the event window-resized.  for now,
                just don't allow it
    */
    vk_widget_set_surface(VK_WIDGET(menu), VWINDOW(vwnd));
    vk_widget_resize(VK_WIDGET(menu), max_width + 2, max_height + 2);

	viper_window_set_key_func(vwnd, vwm_main_menu_ON_KEYSTROKE);
	viper_window_set_userptr(vwnd, (void*)menu);

    viper_event_set(vwnd, "window-close", vwm_main_menu_ON_CLOSE, NULL);
    viper_event_set(vwnd, "term-resized",
        vwm_main_menu_ON_TERM_RESIZED, (void*)menu);

    vk_menu_update(menu);
    vk_widget_draw(VK_WIDGET(menu));
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
    vk_menu_t   *menu;

    (void)anything;

    if(vwnd == NULL) return -1;

    menu = (vk_menu_t *)viper_window_get_userptr(vwnd);

    vk_menu_destroy(menu);

    return 0;
}


int
vwm_main_menu_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd)
{
    vwm_t           *vwm;
    vk_listbox_t    *menu;
    int             retval;

    vwm = vwm_get_instance();

	menu = (vk_listbox_t*)viper_window_get_userptr(vwnd);
    if(keystroke == -1) return -1;

    if(keystroke == vwm->hotkey_menu)
    {
        viper_window_close(vwnd);
        return 0;
    }

    retval = vk_object_push_keystroke(VK_OBJECT(menu), keystroke);

    // user pressed ENTER and something ran
    if(keystroke == 10 && retval == 0)
    {
        viper_window_close(vwnd);

        // return -1 to inidcate keystroke was handled
        return KMIO_HANDLED;
    }

    // vk_menu widget handled the key
    if(retval == 0)
    {
        viper_window_redraw(vwnd);
        return KMIO_HANDLED;
    }

	return keystroke;
}

