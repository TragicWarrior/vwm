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

#include <pseudo.h>
#include <viper.h>

#include "vwm.h"
#include "mainmenu.h"
#include "modules.h"

#define MAX_MENU_ITEMS  128

WINDOW* vwm_main_menu(void)
{
    extern WINDOW   *SCREEN_WINDOW;
	MENU            *menu = NULL;
	WINDOW 		    *window;
	int			    width = 0,height = 0;
    int             screen_height;

	vwm_module_t	*vwm_module;
    char            buf[NAME_MAX];

    gchar			**item_list;
    int             idx = 0;
    int             i;

    // allocate storage for a total of MAX_MENU_ITEMS
    item_list = (gchar**)g_malloc0(sizeof(gchar*) * (MAX_MENU_ITEMS + 1));

    item_list[idx] = g_strdup_printf(" ");
    idx++;

    // iterate through the categories defined in modules.def
    for(i = 0;i < VWM_MOD_TYPE_MAX;i++)
    {
        // skip screensaver type modules.  they are a special class.
        if(i == VWM_MOD_TYPE_SCREENSAVER) continue;

        // print the menu category (type) to the window
        item_list[idx] = g_strdup_printf("%s",modtype_desc[i]);
        idx++;

        vwm_module = NULL;

        do
        {
            if(idx == MAX_MENU_ITEMS) break;

            vwm_module = vwm_module_find_by_type(vwm_module,i);
            if(vwm_module == NULL) break;

            vwm_module_get_title(vwm_module,buf,sizeof(buf) - 1);
            item_list[idx] = g_strdup_printf("..%s",buf);
            idx++;
        }
        while(vwm_module != NULL);

        // add a space before the next menu category
        if(idx < MAX_MENU_ITEMS)
        {
            item_list[idx] = g_strdup_printf(" ");
            idx++;
        }
    }

    menu = viper_menu_create(item_list);
    while(idx != -1)
    {
        g_free(item_list[idx]);
        idx--;
    }
    g_free(item_list);

	// hide character mark on left hand side
	set_menu_mark(menu," ");

    window_get_size_scaled(SCREEN_WINDOW,NULL,&screen_height,0,0.80);

	scale_menu(menu,&height,&width);
	width++;
	if(width < 16) width = 16;

 	// override the default of 1 column X 16 entries per row
    if(height>(screen_height-4)) height=screen_height-4;
	set_menu_format(menu,height,1);

	viper_thread_enter();
	window = viper_window_create(" Menu ",1,2,width,height,TRUE);
    /*
        todo:   it would be nice if the user could resize the menu
                (especially in the horizonal direction) and add more
                columns to the display.  right now, it's not a priority
                (but it would be easy to implement).  just need a few
                lines of code for the event window-resized.  for now,
                just don't allow it
    */
	set_menu_win(menu,window);
	set_menu_fore(menu,VIPER_COLORS(COLOR_WHITE,COLOR_BLUE) | A_BOLD);
	set_menu_back(menu,VIPER_COLORS(COLOR_BLACK,COLOR_WHITE));
	menu_opts_off(menu,O_NONCYCLIC);
	post_menu(menu);
    vwm_menu_marshall(menu,REQ_DOWN_ITEM);

	/* viper_event_set(window,"window-activate",vwm_main_menu_ON_ACTIVATE,NULL); */
	viper_event_set(window,"window-close",vwm_main_menu_ON_CLOSE,
		(gpointer)menu);
	viper_window_set_key_func(window,vwm_main_menu_ON_KEYSTROKE);
	viper_window_set_userptr(window,(gpointer)menu);

	viper_thread_leave();
	return window;
}

/*
gint vwm_main_menu_ON_ACTIVATE(WINDOW *window,gpointer arg)
{
	vwm_post_help(" [Up/Dn] Navigate | [Enter] Select ");
	return 0;
}
*/

gint vwm_main_menu_ON_CLOSE(WINDOW *window,gpointer arg)
{
	viper_thread_enter();
	viper_menu_destroy((MENU*)arg,TRUE);
	viper_thread_leave();

	return VIPER_EVENT_WINDOW_DESIST;
}

gint
vwm_main_menu_ON_KEYSTROKE(gint32 keystroke,WINDOW *window)
{
	MENU            *menu = NULL;
	MEVENT		    mevent;
	gchar		    *item_text = NULL;
	WINDOW		    *new_window;
	vwm_module_t    *vwm_module;

	menu = (MENU*)viper_window_get_userptr(window);
    if(keystroke == -1) return 1;

    viper_thread_enter();

	if(keystroke == KEY_MOUSE)
	{
		menu_driver(menu,keystroke);
        vwm_menu_marshall(menu,REQ_DOWN_ITEM);
		getmouse(&mevent);
		if(mevent.bstate & BUTTON1_DOUBLE_CLICKED) keystroke = KEY_CRLF;
        else
        {
            viper_window_redraw(window);
            viper_thread_leave();
            return 1;
        }
	}

    if(keystroke == 's')
    {
        viper_thread_enter();
        /*
            new_window = viper_filedlg_create(window,
                " Pick a file... ",0.5,0.5,0.5,0.5,
                NULL,FILEDLG_VIEW_EXTENDED);
            new_window = viper_filedlg_create(window,
                " Pick a file... ",0.5,0.5,0.5,0.5,
                NULL,FILEDLG_VIEW_BASIC | FILEDLG_SHOW_CTIME);
        */
        new_window = viper_filedlg_create(window," Pick a file... ",
			0.5,0.5,0.8,0.5,NULL,FILEDLG_FULL);
        viper_window_set_top(new_window);
        viper_thread_leave();
    }

	if(keystroke == KEY_UP)
    {
        menu_driver(menu,REQ_UP_ITEM);
        vwm_menu_marshall(menu,REQ_UP_ITEM);
    }

	if(keystroke == KEY_DOWN)
    {
        menu_driver(menu,REQ_DOWN_ITEM);
        vwm_menu_marshall(menu,REQ_DOWN_ITEM);
    }

	if(keystroke == KEY_CRLF)
	{
        item_text = (gchar*)item_name(current_item(menu));
		vwm_module = vwm_module_find_by_title(&item_text[2]);
		if(vwm_module != NULL)
		{
			// new_window = vwm_module->mod_main(vwm_module->anything);
            new_window = vwm_module_exec(vwm_module);
            viper_window_close(window);
			if(new_window != NULL) viper_window_set_top(new_window);
			viper_thread_leave();
			return 1;
		}
	}

	viper_window_redraw(window);
	viper_thread_leave();
	return 1;
}

/* this function make sure that the user selection is valid--not a category
   or white space */
void
vwm_menu_marshall(MENU *menu,gint32 key_vector)
{
    gchar   *item_text;

    if(key_vector != REQ_UP_ITEM && key_vector != REQ_DOWN_ITEM) return;

    do
    {
        item_text = (gchar*)item_name(current_item(menu));
        if(memcmp(item_text,"..",2)==0) break;
        menu_driver(menu,key_vector);
    }
    while(1);

    return;
}

gint
vwm_main_menu_hotkey(void)
{
	WINDOW *window;

	window = viper_window_find_by_class(vwm_main_menu);

	if(window != NULL)
	{
		viper_thread_enter();
		viper_window_close(window);
		viper_thread_leave();
		return 0;
	}

	viper_thread_enter();
	window = vwm_main_menu();
	viper_window_set_class(window,vwm_main_menu);
	viper_window_set_top(window);
	viper_thread_leave();

	return 0;
}
