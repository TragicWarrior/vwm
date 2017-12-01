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

#define MAX_MENU_ITEMS  128

vwnd_t vwnd*
vwm_main_menu(void)
{
    vk_listbox_t    *menu;
    vwnd_t          *vwnd;
	int			    width = 0,height = 0;
    int             screen_height;

	vwm_module_t	*vwm_module;
    char            buf[NAME_MAX];

    int             i;

    // allocate storage for a total of MAX_MENU_ITEMS
    menu = vk_listbox_create(8, 12);

    // iterate through the categories defined in modules.def
    for(i = 0;i < VWM_MOD_TYPE_MAX;i++)
    {
        vwm_module = NULL;

        do
        {
            if(idx == MAX_MENU_ITEMS) break;

            vwm_module = vwm_module_find_by_type(vwm_module, i);
            if(vwm_module == NULL) break;

            vwm_module_get_title(vwm_module, buf, sizeof(buf) - 1);
            vk_listbox_add_item(buf);
        }
        while(vwm_module != NULL);
    }

	vwnd = viper_window_create(CURRENT_SCREEN_ID, " Menu ", 1, 2,
        width, height, TRUE);
    /*
        todo:   it would be nice if the user could resize the menu
                (especially in the horizonal direction) and add more
                columns to the display.  right now, it's not a priority
                (but it would be easy to implement).  just need a few
                lines of code for the event window-resized.  for now,
                just don't allow it
    */
    vk_widget_set_canvas(VK_WIDGET(menu), VWINDOW(vwnd));

	viper_event_set(vwnd, "window-close",
        vwm_main_menu_ON_CLOSE, (void*)menu);
	viper_window_set_key_func(vwnd, vwm_main_menu_ON_KEYSTROKE);
	viper_window_set_userptr(vwnd, (void*)menu);

	return window;
}

/*
gint vwm_main_menu_ON_ACTIVATE(WINDOW *window,gpointer arg)
{
	vwm_post_help(" [Up/Dn] Navigate | [Enter] Select ");
	return 0;
}
*/

int
vwm_main_menu_ON_CLOSE(vwnd_t *vwnd, void *arg)
{
	// viper_menu_destroy((MENU*)arg, TRUE);

    vk_listbox_destroy(VK_LISTBOX(arg));

	return 0;
}

int
vwm_main_menu_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd)
{
    vk_listbox_t    *menu;

	menu = (MENU*)viper_window_get_userptr(vwnd);
    if(keystroke == -1) return 1;

    vk_object_push_kmio(VK_OBJECT(menu), keystroke);

	return 1;
}

/* this function make sure that the user selection is valid--not a category
   or white space */
void
vwm_menu_marshall(MENU *menu, int32_t key_vector)
{
    char    *item_text;

    if(key_vector != REQ_UP_ITEM && key_vector != REQ_DOWN_ITEM) return;

    do
    {
        item_text = (char*)item_name(current_item(menu));
        if(memcmp(item_text, "..", 2) == 0) break;
        menu_driver(menu, key_vector);
    }
    while(1);

    return;
}

int
vwm_main_menu_hotkey(void)
{
	vwnd_t  *vwnd;

	vwnd = viper_window_find_by_class(-1, TRUE, vwm_main_menu);

	if(vwnd != NULL)
	{
		viper_window_close(vwnd);

		return 0;
	}

	vwnd = vwm_main_menu();
	viper_window_set_class(vwnd, vwm_main_menu);
	viper_window_set_top(vwnd);

	return 0;
}
