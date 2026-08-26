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

/* interior columns that are chrome rather than terminal (the scrollbar) --
   handed to vwm_window_decorate so its border size label reports the real
   vterm size, not the interior width (which now includes the bar) */
static const int vwmterm_frame_reserved = 1;

static short
vwmterm_pair_selector(vterm_t *vterm, short fg, short bg)
{
    (void)vterm;
    return vdk_color_pair(fg, bg);
}

/*
    scroll_info callback for the window's vertical scrollbar.  libviper calls
    this each time it updates the scroller (once per window paint) to learn the
    scroll range and current position.  The terminal still drives the actual
    scrolling, so this only reads vwmterm state back out.

    Range is the history buffer (same figure the wheel / Alt+PgUp math uses);
    position is the top-of-view line within it -- history_sz - height -
    scroll_offset -- which is 0 when scrolled fully back and history_sz -
    height (bottom) when live.  It also tints the bar to the window's frame
    accent colour (on black) so it reads as part of the frame.
*/
static void
vwmterm_scroll_info(vk_widget_t *source, int *content_h, int *content_w,
    int *scroll_y, int *scroll_x)
{
    vwm_t           *vwm;
    vwmterm_data_t  *vwmterm_data;
    vterm_t         *vterm;
    int             width, height;
    int             used;
    int             offset;

    if(content_h != NULL) *content_h = 0;
    if(content_w != NULL) *content_w = 0;
    if(scroll_y != NULL) *scroll_y = 0;
    if(scroll_x != NULL) *scroll_x = 0;

    vwmterm_data = (vwmterm_data_t *)vk_widget_get_userptr(source);
    if(vwmterm_data == NULL) return;

    vterm = vwmterm_data->vterm;
    if(vterm == NULL) return;

    /* tint the bar to the window's frame accent -- magenta when focused, cyan
       otherwise (matching vwm_window_decorate) -- on a black field */
    vwm = vwm_get_instance();
    if(vwm != NULL && vwmterm_data->scroller != NULL)
    {
        if(vk_deck_get_top(vwm->deck) == source)
            vk_scroller_set_border_colors(vwmterm_data->scroller,
                COLOR_MAGENTA, COLOR_BLACK);
        else
            vk_scroller_set_border_colors(vwmterm_data->scroller,
                COLOR_CYAN, COLOR_BLACK);
    }

    /* the alternate screen buffer (vim, less, myman, ...) has no scrollback --
       report an empty range so the bar shows a full, static thumb */
    if(vterm_get_active_buffer(vterm) == VTERM_BUF_ALTERNATE)
    {
        if(content_h != NULL) *content_h = 0;
        if(scroll_y != NULL) *scroll_y = 0;
        return;
    }

    vterm_wnd_size(vterm, &width, &height);
    used = vterm_get_history_used(vterm);

    /* the scroller's content is the scrollback (used rows) plus the live
       screen (height rows); the viewport is height.  scroll position 0 =
       oldest at top, used = live at the bottom.  scroll_offset counts back
       from live, so the position is used - scroll_offset. */
    offset = used - vwmterm_data->scroll_offset;
    if(offset < 0) offset = 0;

    if(content_h != NULL) *content_h = used + height;
    if(scroll_y != NULL) *scroll_y = offset;
}

/*
    Window decorator for vterm windows: the standard vwm chrome plus a redraw
    of the scrollbar.  decorate runs on every window paint -- including a bare
    focus change, which repaints the frame but not the content-attached
    scroller -- so refreshing the bar here keeps its focus-dependent colour
    (set in vwmterm_scroll_info) in lockstep with the border.
*/
static void
vwmterm_decorate(vk_window_t *window, WINDOW *canvas, void *anything)
{
    vwmterm_data_t  *vwmterm_data = (vwmterm_data_t *)anything;

    vwm_window_decorate(window, canvas, (void *)&vwmterm_frame_reserved);

    if(vwmterm_data != NULL && vwmterm_data->scroller != NULL)
    {
        if(vk_scroller_update(vwmterm_data->scroller) > 0)
            vk_widget_draw(VK_WIDGET(vwmterm_data->scroller));
    }
}

int
vwm_mod_init(const char *modpath)
{
    vwmterm_mod_t   *mod;
    void            *dynlib;

    (void)modpath;

    vwmterm_init_keycodes();

    /*
        libutil used to export forkpty/openpty.  glibc 2.34 folded it
        into libc and distros dropped the unversioned libutil.so
        symlink, so a hard dlopen("libutil.so") fails even when the
        symbols are already in the process (vwm is linked -lutil, and
        libc itself provides forkpty).  Try the historical name, then
        the SONAME, then accept a libc that already has forkpty.
    */
    dynlib = dlopen("libutil.so", RTLD_LAZY | RTLD_GLOBAL);
    if(dynlib == NULL)
        dynlib = dlopen("libutil.so.1", RTLD_LAZY | RTLD_GLOBAL);

    if(dynlib == NULL && dlsym(RTLD_DEFAULT, "forkpty") == NULL)
    {
        fprintf(stderr, "[EE] Could not load libutil.so, and libc does not "
            "provide forkpty\n\r");
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
    mod->flags = VTERM_FLAG_XTERM;

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
        /* Per-app default size from Manage Apps (cells); fall back to
           classic 80x25 when unset or non-positive.  Frame chrome is
           width+3 cols (border + scrollbar) by height+2 rows (border),
           so clamp so the window still fits the host screen. */
        width = mod->term_width > 0 ? mod->term_width : 80;
        height = mod->term_height > 0 ? mod->term_height : 25;

        if(width + 3 > scr_width)
            width = scr_width - 3;
        if(height + 2 > scr_height)
            height = scr_height - 2;
        if(width < 1) width = 1;
        if(height < 1) height = 1;
    }
    else
    {
        width = scr_width;
        height = scr_height;
    }

    vterm = vterm_alloc();
    vterm_set_exec(vterm, vwmterm_mod->bin_path, vwmterm_mod->exec_args);
    /* VTERM_FLAG_EXTMOUSE: vwm owns the real terminal's mouse (vk_kmio
       enables SGR tracking with raw escapes + mousemask(0) so it can parse
       reports itself).  Without this, libvterm's mouse driver sees
       has_mouse()==FALSE and seizes mousemask(ALL) when a child app (e.g.
       mc) enables mouse, re-cooking events and breaking vk_kmio's parser.
       VTERM_FLAG_START_HOME (per-app, Manage Apps) chdirs the child into
       $HOME before exec instead of inheriting the host cwd. */
    {
        uint32_t flags = vwmterm_mod->flags | VTERM_FLAG_EXTMOUSE;

        if(mod->start_home)
            flags |= VTERM_FLAG_START_HOME;

        vterm_init(vterm, width, height, flags);
    }
    vterm_set_pair_selector(vterm, vwmterm_pair_selector);
    vterm_set_colors(vterm, COLOR_WHITE, COLOR_BLACK);
    vwmterm_bind_vterm(vterm);

    /* per-app scrollback override (Manage Apps); 0 keeps vterm's default
       history of 4x the terminal height */
    if(mod->scrollback > 0)
        vterm_set_history_size(vterm, mod->scrollback);

    vterm_init_sigio(vterm);

    char title[64] = "";

    if(vwmterm_mod->fullscreen == FALSE)
    {
        char raw_title[62];
        vwm_module_get_title(mod, raw_title, sizeof(raw_title));
        snprintf(title, sizeof(title), " %s ", raw_title);

        /* one extra interior column holds the scrollbar, so the vterm keeps
           the full requested width and the frame reports the real terminal
           size.  vwmterm_frame_reserved tells the decorator to discount that
           column in the [w x h] label it paints on the border. */
        window = vk_window_create(width + 3, height + 2);
        vk_window_set_title(window, title);
        vk_window_set_border_style(window, VK_BORDER_SINGLE);

        content = vk_widget_create(width + 1, height);
        vk_widget_set_state(content,
            vk_widget_get_state(content) | VK_STATE_EXPAND);
    }
    else
    {
        window = vk_window_create(width, height);
        vk_window_set_border_style(window, VK_BORDER_NONE);

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
    memcpy(vwmterm_data->title, title, sizeof(vwmterm_data->title));
    vwmterm_data->state = VWMTERM_STATE_RUNNING;

    vk_widget_set_userptr(VK_WIDGET(window), (void *)vwmterm_data);

    ctx_vwmterm->anything = (void *)vwmterm_data;
    ctx_vwmterm->shutdown = &shutdown;

    vk_object_register_event(VK_OBJECT(window), VWM_EVENT_ON_CLOSE,
        vwmterm_ON_CLOSE, (void *)vwmterm_data);
    /*
        Register ON_RESIZE with vwmterm_data, not the raw vterm pointer.
        Capturing vterm here makes the handler's payload dangle as soon
        as vterm_destroy runs in ON_CLOSE -- the very UAF that crashed
        the active-buffer realloc.  Routing through vwmterm_data lets
        the handler read vwmterm_data->vterm fresh and bail when it is
        NULL (set by ON_CLOSE after destroy).
    */
    vk_object_register_event(VK_OBJECT(window), VK_EVENT_ON_RESIZE,
        vwmterm_ON_RESIZE, (void *)vwmterm_data);

    /*
        ON_RECREATE fires after a teleport, when libviper rebuilds the
        widget's underlying ncurses WINDOW in the new SCREEN.  the cached
        WINDOW pointer in libvterm is then dead -- rebind to the fresh
        canvas and force a full redraw.
    */
    vk_object_register_event(VK_OBJECT(window), VK_EVENT_ON_RECREATE,
        vwmterm_ON_RECREATE, (void *)vwmterm_data);

    /*
        Attach a vertical scrollbar to the content widget's last column.  The
        content is one column wider than the vterm, so that column is free
        while the vterm keeps its full width.  The content is a plain widget,
        so -- unlike a window or listbox -- libviper won't drive
        the scroller for us; vwmterm_window_update() re-composites it by hand
        on every repaint.  scroll_source is the window, whose userptr is the
        vwmterm_data the scroll_info callback reads.  The bar is display-only:
        the wheel / Alt+PgUp paths keep driving scroll_offset.  A fullscreen
        terminal has no scrollbar.
    */
    if(vwmterm_mod->fullscreen == FALSE)
    {
        vwmterm_data->scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
        vk_scroller_set_border_style(vwmterm_data->scroller, VK_BORDER_SINGLE);
        /* keep the trough on screen even with no scrollback -- a full-height
           thumb when there's nothing to scroll (xfce4-style) */
        vk_scroller_set_always_visible(vwmterm_data->scroller, 1);
        vk_widget_set_attrs(VK_WIDGET(vwmterm_data->scroller), A_BOLD);
        vk_scroller_set_scroll_source(vwmterm_data->scroller,
            VK_WIDGET(window));
        vk_scroller_set_scroll_info(vwmterm_data->scroller,
            vwmterm_scroll_info);
        vk_widget_attach_scroller(content, vwmterm_data->scroller);

        /* pre-draw so the bar is present on the first composite, before any
           child output triggers a repaint */
        if(vk_scroller_update(vwmterm_data->scroller) > 0)
            vk_widget_draw(VK_WIDGET(vwmterm_data->scroller));

        /* decorate now that the scroller exists -- vwmterm_decorate redraws
           the bar on every window paint so its colour tracks focus */
        vk_window_set_decorate(window, vwmterm_decorate, (void *)vwmterm_data);
    }

    if(vwmterm_mod->fullscreen == FALSE)
    {
        int pos_x = (scr_width - (width + 3)) / 2;
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
