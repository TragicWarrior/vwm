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

#include <string.h>

#include <viper.h>

#include "wndlist.h"
#include "vwm.h"


WINDOW* vwm_fmod_wndlist(gpointer anything)
{
	const char      *title=" Window List ";
	WINDOW		    *window;
	int			    width = 0,height = 0;
	MENU			*menu;
	ITEM			**item_list;
	gchar			**titles;
	guint			item_count;

	if(viper_window_find_by_class((gpointer)vwm_fmod_wndlist) != NULL)
        return NULL;

	// viper_thread_enter();

	titles = viper_deck_get_wndlist();
	item_count = g_strv_length(titles);

	if(item_count == 0)
	{
		// viper_thread_leave();
		return NULL;
	}

	menu = viper_menu_create(titles);

	item_list = (ITEM**)g_malloc0(sizeof(ITEM*)*(item_count+1));

	// override the default of 1 column X 16 entries per row
	set_menu_format(menu,20,1);
	// hide character mark on left hand side
	set_menu_mark(menu," ");

	scale_menu(menu,&height,&width);
	width++;
	if((strlen(title) + 10) > width) width = (strlen(title) + 10);

	window = viper_window_create((gchar*)title,0.95,2,width,height,TRUE);
	viper_menu_bind(menu,window,0,0,width,height);

//	set_menu_sub(menu,window);
	set_menu_fore(menu,
		VIPER_COLORS(COLOR_MAGENTA,COLOR_WHITE) | A_REVERSE | A_BOLD);
	set_menu_back(menu,VIPER_COLORS(COLOR_BLACK,COLOR_WHITE));

//	post_menu(menu);

/*	viper_event_set(window,"window-activate",vwm_fmod_wndlist_ON_ACTIVATE,NULL); */
	viper_event_set(window,"window-destroy",vwm_fmod_wndlist_ON_DESTROY,
		(gpointer)menu);
	viper_window_set_key_func(window,vwm_fmod_wndlist_ON_KEYSTROKE);
	viper_window_set_userptr(window,(gpointer)menu);
	viper_window_set_state(window,STATE_EMINENT);

	// viper_thread_leave();
	g_strfreev(titles);

	return window;
}

/*
gint vwm_fmod_wndlist_ON_ACTIVATE(WINDOW *window,gpointer arg)
{
	vwm_post_help(" [Up/Dn] Navigate | [Enter] Select ");
	return 0;
}
*/

gint vwm_fmod_wndlist_ON_DESTROY(WINDOW *window,gpointer arg)
{
	MENU	*menu;
//	ITEM	**items;
//	gint	count;
//	gchar	*text;
//	gint	i;

	menu=(MENU*)arg;
	// viper_thread_enter();

/*
	unpost_menu(menu);
	count=item_count(menu);
	items=menu_items(menu);
	free_menu(menu);
	for(i=0;i<count;i++)
	{
		text=(gchar*)item_name(items[i]);
		if(free_item(items[i])!=E_OK) flash();
		g_free(text);
	}
	g_free(items);
*/

	viper_menu_destroy(menu,TRUE);

	// viper_thread_leave();

	return 0;
}

gint vwm_fmod_wndlist_ON_KEYSTROKE(gint32 keystroke,WINDOW *window)
{
	MENU        *menu = NULL;
	MEVENT		mevent;
	// ITEM		*item;
	// guint32	window_state;

	menu = (MENU*)viper_window_get_userptr(window);

	if(keystroke == KEY_MOUSE)
	{
		menu_driver(menu,keystroke);
		getmouse(&mevent);
		if((mevent.bstate & BUTTON1_DOUBLE_CLICKED) == BUTTON1_DOUBLE_CLICKED)
			keystroke = KEY_CRLF;
	}

	// viper_thread_enter();
	if(keystroke == KEY_UP) menu_driver(menu,REQ_UP_ITEM);
	if(keystroke == KEY_DOWN) menu_driver(menu,REQ_DOWN_ITEM);
	if(keystroke == KEY_CRLF)
	{
		// item = current_item(menu);
		/* viper_wnd=(VIPER_WND*)item_userptr(item); */
		viper_window_destroy(window);
/*
		if((viper_wnd->window_state & STATE_VISIBLE)==FALSE)
			viper_window_unhide(viper_wnd->window);
		viper_window_set_top(viper_wnd->window);
		viper_window_focus(TOPMOST_WINDOW);
*/
		/* viper_screen_redraw(REDRAW_ALL); */
		// viper_thread_leave();
		return 1;
	}
	
	viper_window_redraw(window);
	// viper_thread_leave();
	return 1;
}
