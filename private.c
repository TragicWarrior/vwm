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
#include <inttypes.h>

#include <pseudo.h>

#include "vwm.h"
#include "modules.h"
#include "mainmenu.h"
#include "private.h"
#include "bkgd.h"
#include "wndlist.h"

static WINDOW*
vwm_fmod_exit(vwm_module_t *mod);

gint
vwm_default_border_agent_focus(WINDOW *window,gpointer anything)
{
	WINDOW			*border_wnd;
	const gchar		*title;
	guint32			window_state;
 	gint		   	y,x;

	border_wnd = viper_get_window_frame(window);
	title = viper_window_get_title(window);

    window_decorate(border_wnd,(gchar*)title,TRUE);
    getmaxyx(border_wnd,y,x);
	mvwprintw(border_wnd,0,x - sizeof("[.OX]") + 1,"[.OX]");

	window_state = viper_window_get_state(window);
    if(window_state & STATE_NORESIZE) mvwaddch(border_wnd,y - 1,x - 1,'*');

    window_modify_border(border_wnd,A_BOLD,
    viper_color_pair(COLOR_WHITE,COLOR_MAGENTA));

   return 0;
}

gint
vwm_default_border_agent_unfocus(WINDOW *window,gpointer anything)
{
	WINDOW		    *border_wnd;
	const gchar	    *title;
	guint32		    window_state;
 	gint		    y,x;

	border_wnd = viper_get_window_frame(window);
	title = viper_window_get_title(window);

    window_decorate(border_wnd,(gchar*)title,TRUE);
    getmaxyx(border_wnd,y,x);
	mvwprintw(border_wnd,0,x - sizeof("[.OX]") + 1,"[.OX]");

	window_state = viper_window_get_state(window);
    if(window_state & STATE_NORESIZE) mvwaddch(border_wnd,y - 1,x - 1,'*');

    window_modify_border(border_wnd,A_NORMAL,
	viper_color_pair(COLOR_BLACK,COLOR_CYAN));

   return 0;
}

void
vwm_modules_preload(void)
{
    // VWM         *vwm;
    WINDOW          *msgbox;
    gchar           *error_msg;
    gchar           *module_dirs[] = {NULL,_VWM_SHARED_MODULES};
    vwm_module_t    *fake_mod;
    gint            array_sz;
    int             i;

	// vwm = vwm_get_instance();

    array_sz = sizeof(module_dirs)/sizeof(module_dirs[0]);
    module_dirs[0] = VWM_MOD_DIR;

    for(i = 0;i < array_sz;i++)
    {
        error_msg = vwm_modules_load(module_dirs[i]);

        if(error_msg != NULL)
        {
            // viper_thread_enter();
            msgbox = viper_msgbox_create(" Module Warning! ",
                0.5,0.5,0,0,error_msg,
                MSGBOX_ICON_WARN | MSGBOX_TYPE_OK);
            viper_window_show(msgbox);
            // viper_thread_leave();
            g_free(error_msg);
        }
    }

 	/* these "fake" modules will appear on the menu in the same order whereby
		they were added below	*/
	//vwm_module_add("VWM","Window List",vwm_fmod_wndlist,NULL,
	//	"vwm_fmod_winlist");

    fake_mod = vwm_module_create();
    vwm_module_set_title(fake_mod,"Exit");
    vwm_module_set_type(fake_mod,VWM_MOD_TYPE_SYSTEM);
    fake_mod->main = vwm_fmod_exit;
    fake_mod->anything = (void*)"shutdown vwm";

    vwm_module_add(fake_mod);

/*
	vwm_module_add("VWM","Screensaver",vwm_fmod_scrsaver,NULL,
		"vwm_fmod_scrsaver");
*/

	// vwm_module_add("VWM","Exit",vwm_fmod_exit,NULL,
	//	"vwm_fmod_exit");

/*
	// special handling for default screensaver
	vwm_module_add(VWM_SCREENSAVER,"SysSaver",vwm_fmod_syssaver,
      NULL,"vwm_fmod_sysinfo");
   // vwm_scrsaver_start();
*/

	return;
}

WINDOW*
vwm_fmod_exit(vwm_module_t *mod)
{
    extern int      shutdown;

    shutdown = 1;

	// shmq_msg_put("ui",(void*)mod->anything);

	return NULL;
}
