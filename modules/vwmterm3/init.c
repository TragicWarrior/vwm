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

#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <viper.h>
#include <vterm.h>
#include <protothread.h>

#include "vwmterm.h"
#include "events.h"
#include "vwmterm_thd.h"
#include "signals.h"

#include "../../vwm.h"
#include "../../modules.h"

int
vwm_mod_init(vwm_module_t *mod);

static vwnd_t*
vwmterm_main(vwm_module_t *mod);

// constructor
int
vwm_mod_init(vwm_module_t *mod)
{
    vwmterm_data_t  *vwmterm_data;
    void            *dynlib;
    int             retval;

	// preload libutil for use with this module.
	dynlib = dlopen("libutil.so",RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL) return -1;

    // preload libvterm for use with this module
    dynlib = dlopen("libvterm.so",RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL) return -1;

	// configure and register module for color instance
    vwmterm_data = calloc(1, sizeof(vwmterm_data_t));

    mod->main = vwmterm_main;
    vwm_module_set_title(mod, "VTerm (color)");
    vwm_module_set_type(mod, VWM_MOD_TYPE_TOOL);
    vwm_module_set_userptr(mod, (void *)vwmterm_data);
	retval = vwm_module_add(mod);

	// allloc, configure, and register module for vt100 instance
    vwmterm_data = calloc(1, sizeof(vwmterm_data_t));
    mod = vwm_module_create();

    mod->main = vwmterm_main;
    vwm_module_set_title(mod, "VTerm (vt100)");
    vwm_module_set_type(mod, VWM_MOD_TYPE_TOOL);
    vwmterm_data->flags |= VTERM_FLAG_VT100;
    vwm_module_set_userptr(mod, (void *)vwmterm_data);
	retval = vwm_module_add(mod);

    if(retval != 0) exit(-1);

	return 0;
}

vwnd_t*
vwmterm_main(vwm_module_t *mod)
{
    vterm_t                 *vterm;
	vwnd_t	      	        *vwnd;
	int		      	        width, height;

    extern protothread_t    pt[2];
    pt_context_t            *ctx_vwmterm;
    vwmterm_data_t          *vwmterm_data;
    extern int              shutdown;

    getmaxyx(CURRENT_SCREEN, height, width);
    if(height > 30 && width > 84)
    {
        height = 25;
        width = 80;
    }
    else
    {
        // calculate scaled window size
	    window_get_size_scaled(NULL, &width, &height, 0.85, 0.65);
	    if(width > 80) width = 80;
	    if(height > 25) height = 25;
    }

    vwmterm_data = (vwmterm_data_t*)vwm_module_get_userptr(mod);

    vterm = vterm_create(width, height, vwmterm_data->flags);
    vterm_set_colors(vterm, COLOR_WHITE, COLOR_BLACK);

    // setup SIGIO responsiveness
    vterm_init_sigio(vterm);

    // create window
	vwnd = viper_window_create(CURRENT_SCREEN_ID, TRUE, " VTerm ",
        0.5, 0.5, width, height);
    viper_window_set_resizable(vwnd, TRUE);
	viper_window_set_limits(vwnd, 15, 2, WSIZE_UNCHANGED, WSIZE_UNCHANGED);

    // libviper set the default bkgd OR to WHITE on BLACK.  undo it.
    wbkgdset(VWINDOW(vwnd), 0);
	wattron(VWINDOW(vwnd), VIPER_COLORS(COLOR_WHITE,COLOR_BLACK));

    // init terminal
    vterm_wnd_set(vterm, VWINDOW(vwnd));
    vterm_erase(vterm);

    // allocate thread context and stateful data
    ctx_vwmterm = malloc(sizeof(pt_context_t));
    // vwmterm_data = malloc(sizeof(vwmterm_data_t));

    // initialize stateful data
    vwmterm_data->vwnd = vwnd;
    vwmterm_data->vterm = vterm;
    vwmterm_data->state = VWMTERM_STATE_RUNNING;

    ctx_vwmterm->anything = (void *)vwmterm_data;
    ctx_vwmterm->shutdown = &shutdown;

    // attach event handlers
	viper_event_set(vwnd, "window-resized",
        vwmterm_ON_RESIZE, (void*)vterm);
	viper_event_set(vwnd, "window-close",
        vwmterm_ON_CLOSE, (void*)vterm);
	viper_event_set(vwnd, "window-destroy",
        vwmterm_ON_DESTROY, (void*)vwmterm_data);
	viper_window_set_key_func(vwnd,
        vwmterm_ON_KEYSTROKE);
	viper_window_set_userptr(vwnd, (void*)vterm);

    pt_create(pt[PT_PRIORITY_NORMAL], &ctx_vwmterm->pt_thread,
        vwmterm_thd, ctx_vwmterm);

	return vwnd;
}
