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
#include "pt_thread.h"
#include "signals.h"
#include "module.h"

#include "../../vwm.h"
#include "../../modules.h"
#include "../../private.h"

int
vwm_mod_init(const char *modpath);

static vwnd_t*
vwmterm_main(vwm_module_t *mod);

// constructor
int
vwm_mod_init(const char *modpath)
{
    vwmterm_mod_t   *mod;
    void            *dynlib;

    (void)modpath;

	// preload libutil for use with this module.
	dynlib = dlopen("libutil.so",RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL) return -1;

    // preload libvterm for use with this module
    dynlib = dlopen("libvterm.so",RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL) return -1;

	// configure and register module for color instance
    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-color");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (color)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_RXVT;

	vwm_module_add(VWM_MODULE(mod));

	// allloc, configure, and register module for vt100 instance
    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-vt100");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (vt100)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_VT100;

	vwm_module_add(VWM_MODULE(mod));

	// allloc, configure, and register module for fullscreen  instance
    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-fullscreen");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (fullscreen)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->fullscreen = TRUE;
    mod->flags = VTERM_FLAG_XTERM;

	vwm_module_add(VWM_MODULE(mod));

	// allloc, configure, and register module for xterm instance
    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-xterm");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (xterm)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_XTERM;

	vwm_module_add(VWM_MODULE(mod));

	// allloc, configure, and register module for vt100 instance
    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-xterm256");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (xterm 256)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_XTERM_256;

	vwm_module_add(VWM_MODULE(mod));


	return 0;
}

vwnd_t*
vwmterm_main(vwm_module_t *mod)
{
    vwmterm_mod_t           *vwmterm_mod;
    vwmterm_data_t          *vwmterm_data;
    vterm_t                 *vterm;
	vwnd_t	      	        *vwnd;
	int		      	        width, height;

    extern protothread_t    pt[2];
    pt_context_t            *ctx_vwmterm;
    extern int              shutdown;

    vwmterm_mod = (vwmterm_mod_t *)mod;

    getmaxyx(CURRENT_SCREEN, height, width);

    // if the term is not fullscreen, calculate a sane window size
    if(vwmterm_mod->fullscreen == FALSE)
    {
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
    }

    // vterm = vterm_create(width, height, vwmterm_mod->flags);
    vterm = vterm_alloc();
    vterm_set_exec(vterm, vwmterm_mod->bin_path, vwmterm_mod->exec_args);
    vterm_init(vterm, width, height, vwmterm_mod->flags);
    vterm_set_colors(vterm, COLOR_WHITE, COLOR_BLACK);

    // setup SIGIO responsiveness
    vterm_init_sigio(vterm);

    // create window
    if(vwmterm_mod->fullscreen == FALSE)
    {
	    vwnd = viper_window_create(CURRENT_SCREEN_ID, TRUE, " VTerm ",
            0.5, 0.5, width, height);
        viper_window_set_resizable(vwnd, TRUE);
	    viper_window_set_limits(vwnd, 15, 2, WSIZE_UNCHANGED, WSIZE_UNCHANGED);
    }
    else
    {
        vwnd = viper_window_create(CURRENT_SCREEN_ID, FALSE, " VTerm ",
            0, 0, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
        viper_window_set_resizable(vwnd, FALSE);
    }

    // libviper set the default bkgd OR to WHITE on BLACK.  undo it.
    wbkgdset(VWINDOW(vwnd), 0);
	wattron(VWINDOW(vwnd), VIPER_COLORS(COLOR_WHITE,COLOR_BLACK));

    // init terminal
    vterm_wnd_set(vterm, VWINDOW(vwnd));
    vterm_erase(vterm, -1);

    // allocate thread context and stateful data
    vwmterm_data = (vwmterm_data_t*)calloc(1, sizeof(vwmterm_data_t));
    ctx_vwmterm = malloc(sizeof(pt_context_t));

    // initialize stateful data
    vwmterm_data->vwnd = vwnd;
    vwmterm_data->vterm = vterm;
    vwmterm_data->state = VWMTERM_STATE_RUNNING;

    ctx_vwmterm->anything = (void *)vwmterm_data;
    ctx_vwmterm->shutdown = &shutdown;

    // attach event handlers
    viper_event_set(vwnd, "term-resized",
        vwmterm_ON_SCREEN_RESIZED,
            vwmterm_mod->fullscreen ? (void *)"fullscreen" : NULL);
	viper_event_set(vwnd, "window-resized",
        vwmterm_ON_RESIZE, (void *)vterm);
	viper_event_set(vwnd, "window-close",
        vwmterm_ON_CLOSE, (void *)vwmterm_data);
	viper_window_set_key_func(vwnd,
        vwmterm_ON_KEYSTROKE);
	viper_window_set_userptr(vwnd, (void*)vterm);

    pt_create(pt[PT_PRIORITY_NORMAL], &ctx_vwmterm->pt_thread,
        vwmterm_thd, ctx_vwmterm);

	return vwnd;
}
