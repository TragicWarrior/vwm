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

#include <vdk.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"
#include "pt_thread.h"
#include "signals.h"
#include "module.h"

#include "../../vwm.h"
#include "../../modules.h"
#include "../../private.h"
#include "../../panel.h"
#include "../../winman.h"
#include "../../protothread.h"
#include "../../sched.h"

int
vwm_mod_init(const char *modpath);

static vk_window_t*
vwmterm_main(vwm_module_t *mod);

static short
vwmterm_pair_selector(vterm_t *vterm, short fg, short bg)
{
    (void)vterm;
    return vdk_color_pair(fg, bg);
}

int
vwm_mod_init(const char *modpath)
{
    vwmterm_mod_t   *mod;
    void            *dynlib;

    (void)modpath;

    vwmterm_init_keycodes();

	dynlib = dlopen("libutil.so", RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL)
    {
        fprintf(stderr, "[EE] Could not load libutil.so\n\r");
        return -1;
    }

    dynlib = dlopen("libvterm.so", RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL)
    {
        fprintf(stderr, "[EE] Could not load libvterm.so\n\r");
        return -1;
    }

    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-color");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (color)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_RXVT;

	vwm_module_add(VWM_MODULE(mod));

    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-vt100");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (vt100)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_VT100;

	vwm_module_add(VWM_MODULE(mod));

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

    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-xterm");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (xterm)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_XTERM;

	vwm_module_add(VWM_MODULE(mod));

    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-xterm256");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (xterm 256)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_XTERM_256;

	vwm_module_add(VWM_MODULE(mod));

    mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    VWM_MODULE(mod)->main = vwmterm_main;
    VWM_MODULE(mod)->clone = vwmterm_module_clone;
    VWM_MODULE(mod)->configure = vwmterm_module_configure;
    vwm_module_set_name(VWM_MODULE(mod), "vterm-truecolor");
    vwm_module_set_title(VWM_MODULE(mod), "VTerm (truecolor)");
    vwm_module_set_type(VWM_MODULE(mod), VWM_MOD_TYPE_TOOL);
    mod->flags = VTERM_FLAG_TRUECOLOR;

	vwm_module_add(VWM_MODULE(mod));

	return 0;
}

vk_window_t*
vwmterm_main(vwm_module_t *mod)
{
    vwm_t                   *vwm;
    vwmterm_mod_t           *vwmterm_mod;
    vwmterm_data_t          *vwmterm_data;
    vterm_t                 *vterm;
    vk_window_t             *window;
    vk_widget_t             *content;
	int		      	        width, height;
    int                     scr_width, scr_height;

    extern vwm_sched_t      *sched;
    vwm_sched_ctx_t         *ctx_vwmterm;
    extern int              shutdown;

    vwm = vwm_get_instance();
    vwmterm_mod = (vwmterm_mod_t *)mod;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    if(vwmterm_mod->fullscreen == FALSE)
    {
        if(scr_height > 30 && scr_width > 84)
        {
            height = 25;
            width = 80;
        }
        else
        {
            width = (int)((scr_width + 1) * 0.85);
            height = (int)((scr_height + 1) * 0.65);
		    if(width > 80) width = 80;
		    if(height > 25) height = 25;
        }
    }
    else
    {
        width = scr_width;
        height = scr_height;
    }

    vterm = vterm_alloc();
    vterm_set_exec(vterm, vwmterm_mod->bin_path, vwmterm_mod->exec_args);
    vterm_init(vterm, width, height, vwmterm_mod->flags);
    vterm_set_pair_selector(vterm, vwmterm_pair_selector);
    vterm_set_colors(vterm, COLOR_WHITE, COLOR_BLACK);

    vterm_init_sigio(vterm);

    if(vwmterm_mod->fullscreen == FALSE)
    {
        char raw_title[64];
        char title[68];
        vwm_module_get_title(mod, raw_title, sizeof(raw_title));
        snprintf(title, sizeof(title), " %s ", raw_title);

        window = vk_window_create(width + 2, height + 2);
        vk_window_set_title(window, title);
        vk_window_set_border_style(window, VK_FRAME_SINGLE);
        vk_window_set_decorate(window, vwm_window_decorate, NULL);

        content = vk_widget_create(width, height);
    }
    else
    {
        window = vk_window_create(width, height);
        vk_window_set_border_style(window, VK_FRAME_NONE);

        uint32_t state = vk_widget_get_state(VK_WIDGET(window));
        vk_widget_set_state(VK_WIDGET(window), state | VK_STATE_NORESIZE);

        content = vk_widget_create(width, height);
    }

    wbkgdset(vk_widget_get_canvas(content), 0);
    wattron(vk_widget_get_canvas(content),
        COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLACK)));

    vterm_wnd_set(vterm, vk_widget_get_canvas(content));
    vterm_erase(vterm, -1, ' ');

    vk_window_set_child(window, content);
    vk_object_set_kmio(VK_OBJECT(window), vwmterm_ON_KEYSTROKE);

    vwmterm_data = (vwmterm_data_t*)calloc(1, sizeof(vwmterm_data_t));
    ctx_vwmterm = calloc(1, sizeof(vwm_sched_ctx_t));

    vwmterm_data->window = window;
    vwmterm_data->vterm = vterm;
    vwmterm_data->mod = mod;
    vwmterm_data->state = VWMTERM_STATE_RUNNING;

    vk_widget_set_userptr(VK_WIDGET(window), (void *)vwmterm_data);

    ctx_vwmterm->anything = (void *)vwmterm_data;
    ctx_vwmterm->shutdown = &shutdown;

    vk_object_register_event(VK_OBJECT(window), VWM_EVENT_ON_CLOSE,
        vwmterm_ON_CLOSE, (void *)vwmterm_data);

    if(vwmterm_mod->fullscreen == FALSE)
    {
        int pos_x = (scr_width - (width + 2)) / 2;
        int pos_y = (scr_height - (height + 2)) / 2;
        vk_widget_move(VK_WIDGET(window), pos_x, pos_y);
    }
    else
    {
        vk_widget_move(VK_WIDGET(window), 0, 0);
    }

    vwm_sched_task_create(sched, ctx_vwmterm, vwmterm_thd, VWM_SCHED_NORMAL);

    vwm_panel_set_status(VWM_WINDOW_HELP);

	return window;
}
