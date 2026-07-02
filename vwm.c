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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <locale.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __linux
#include <sys/klog.h>
#endif

#include <vdk.h>
#include <vkmio.h>
#include "protothread.h"
#include "sched.h"

#include "vwm.h"
#include "private.h"
#include "bkgd.h"
#include "mainmenu.h"
#include "panel.h"
#include "modules.h"
#include "settings.h"
#include "signals.h"
#include "winman.h"
#include "list.h"
#include "clock.h"
#include "poll_input_thd.h"
#include "programs.h"

static void
vwm_cursor_overlay(vk_screen_t *screen, int surface_id, WINDOW *canvas);

static int
vwm_on_surface_change(vk_object_t *object, int event, void *anything);

static int
vwm_on_teleport(vk_object_t *object, int event, void *anything);

static void
vwm_sched_render(void *arg);

vwm_sched_t             *sched = NULL;
int                     shutdown = 0;

// store argv and argc for use elsewhere (with modules)
char    **vwm_argv;
int     vwm_argc;

int main(int argc,char **argv)
{
    vwm_t                   *vwm = NULL;
    extern char             **vwm_argv;
    extern int              vwm_argc;
	int		      		    fd;
	char		      		*locale = NULL;
	int						flags;

    extern int              shutdown;
    extern vwm_sched_t      *sched;

    vwm_sched_ctx_t         *ctx_clock;
    vwm_sched_ctx_t         *ctx_poll_input;
    MEVENT                  mouse_event;

    sched = vwm_sched_init();

    // setup clock task (NORMAL priority)
    ctx_clock = calloc(1, sizeof(vwm_sched_ctx_t));
    ctx_clock->shutdown = &shutdown;

    // setup input-polling task (HIGH priority)
    ctx_poll_input = calloc(1, sizeof(vwm_sched_ctx_t));
    ctx_poll_input->anything = &mouse_event;
    ctx_poll_input->shutdown = &shutdown;

    vwm_sched_task_create(sched, ctx_clock,
                vwm_clock_driver, VWM_SCHED_NORMAL);
    vwm_sched_task_create(sched, ctx_poll_input,
                vwm_poll_input, VWM_SCHED_HIGH);

    vwm_argc = argc;
    vwm_argv = argv;

    {
        bool ignore_tty_size = false;
        int i;

        for(i = 1; i < argc; i++)
        {
            if(strcmp(argv[i], "--ignore-tty-size") == 0)
            {
                ignore_tty_size = true;
                break;
            }
        }

        if(!ignore_tty_size)
        {
            struct winsize ws;

            if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0)
            {
                if(ws.ws_col < 80 || ws.ws_row < 25)
                {
                    fprintf(stderr,
                        "vwm: terminal too small (%dx%d). "
                        "Minimum size is 80x25.\n"
                        "Use --ignore-tty-size to bypass "
                        "this check.\n",
                        ws.ws_col, ws.ws_row);
                    return 1;
                }
            }
        }
    }

	/*
        set the locale to the default settings (as configured by env).
		this is required for ncurses to work properly.
    */
    locale = getenv("LANG");
    if(locale == NULL) locale = "en_US.UTF-8";

    setlocale(LC_ALL, locale);

	// print some debug information
	printf("%s\n\r", locale);
	printf("ncurses = %d.%d (%d)\n\r", NCURSES_VERSION_MAJOR,
		NCURSES_VERSION_MINOR,NCURSES_VERSION_PATCH);
	fflush(NULL);

#ifdef __linux
    // suppress printk messages.  klogctl() is linux specific.
	klogctl(6, NULL, 0);
    printf("VWM running on Linux\n\r");
#endif

    // supress STDERR
	fd = open("/dev/null", O_WRONLY);
	if(fd == -1) exit(0);
	dup2(fd, STDERR_FILENO);

	// ignore terminal interrupt signal
    vwm_sigset(SIGINT, SIG_IGN);

    // unwind cleanly on SIGTERM so the terminal gets restored
    vwm_sigset(SIGTERM, vwm_SIGTERM);
    vwm_sigset(SIGPIPE, SIG_IGN);

#ifdef _DEBUG
    vwm_sigset(SIGILL, vwm_backtrace);
    vwm_sigset(SIGSEGV, vwm_backtrace);
    vwm_sigset(SIGFPE, vwm_backtrace);
#endif

	vwm_sigset(SIGIO, vwm_SIGIO);
	fcntl(STDIN_FILENO,F_SETOWN, getpid());
	flags = fcntl(STDIN_FILENO, F_GETFL);
	fcntl(STDIN_FILENO,F_SETFL, flags | FASYNC);

	// use the integrated window manager
	vwm = vwm_init();

    /* now that newterm() has run (inside vwm_init), claim SIGWINCH so a
       same-size dtach reattach still drives the resync cascade -- chains
       ncurses' own handler, so ordinary resizes are unaffected. */
    vwm_sigwinch_install();

    vwm_panel_init(vwm);

    vk_screen_refresh(vwm->screen);

    vwm_modules_preload(vwm);
    vwm_menubar_init();
    vwm_settings_load(vwm);
    vwm_apply_surface_count(vwm->surface_count);
    /* settings + surfaces are now both live; seed each desktop's bkgd */
    vwm_apply_desktop_bkgd_all();
    /* the first vk_screen_refresh above cached the active desktop's
       wallpaper using the built-in defaults, before settings_load
       supplied the saved pattern/color.  Drop that stale cache so the
       refresh below rebuilds it from the loaded settings -- otherwise
       the real wallpaper does not appear until the first resize. */
    vwm_invalidate_wallpaper_cache_all();
    vwm_programs_load(vwm);

    vk_screen_refresh(vwm->screen);

    /* coalesce vterm composites: drain tasks mark the screen dirty and
       this hook issues one refresh per scheduler step (see item 5). */
    vwm_sched_set_step_cb(sched, vwm_sched_render, vwm);

    vwm_sched_run(sched, &shutdown);

    vwm_sched_deinit(sched);

    vk_kmio_shutdown(vk_screen_get_fd(vwm->screen));
    vk_screen_destroy(vwm->screen);
    fsync(fd);
	close(fd);

	return 0;
}

/*
    scheduler per-step render hook.  the vterm drain tasks update their
    own windows and set vwm->screen_dirty rather than each compositing
    the whole screen; this fires once per step and collapses N busy
    tiles into a single vk_screen_refresh.  cheap no-op when nothing
    drained this step.
*/
static void
vwm_sched_render(void *arg)
{
    vwm_t   *vwm = (vwm_t *)arg;

    if(vwm->screen_dirty)
    {
        vk_screen_refresh(vwm->screen);
        vwm->screen_dirty = 0;
    }
}

vwm_t*
vwm_init(void)
{
	static vwm_t    *vwm = NULL;

	if(vwm == NULL)
	{
 		vwm = (vwm_t*)calloc(1, sizeof(vwm_t));

        vwm->screen = vk_screen_create();
        vdk_color_init();
        vwm_input_rearm(vwm);

        vk_screen_set_wallpaper(vwm->screen, vwm_bkgd_simple_normal);

        strncpy(vwm->task_indicator_action, "none", NAME_MAX - 1);
        strncpy(vwm->date_click_action, "calendar", NAME_MAX - 1);
        vwm->screensaver_cmd[0] = '\0';
        vwm->screensaver_timeout = 0;
        vwm->clipboard_mode = VWM_CLIPBOARD_BOTH;
        vwm->show_hostname = 0;             /* off by default */
        vwm->hostname_fg = COLOR_WHITE;
        vwm->hostname_bg = COLOR_BLUE;
        vwm->hostname_font = -1;            /* Basic (plain one-row label) */
        vwm->hostname_fill = 0;             /* full block */
        {
            /* sensible defaults per desktop -- diverse colors so a
               fresh vwm still has the original per-surface identity */
            short defaults[VWM_MAX_DESKTOPS] = {
                COLOR_BLUE, COLOR_RED, COLOR_CYAN,
                COLOR_GREEN, COLOR_MAGENTA, COLOR_YELLOW
            };
            int   k;
            for(k = 0; k < VWM_MAX_DESKTOPS; k++)
            {
                vwm->desktop_color[k] = defaults[k];
                vwm->desktop_fg[k] = COLOR_BLACK;
                vwm->desktop_wallpaper[k] = VWM_WALLPAPER_STIPLE;
            }
        }
        vwm->surface_count = 3;

        vwm->decks = calloc(vwm->surface_count, sizeof(vk_deck_t *));

        vwm->decks[0] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[0], true);
        vk_screen_attach_widget(vwm->screen, 0, VK_WIDGET(vwm->decks[0]));
        vk_object_register_event(VK_OBJECT(vwm->decks[0]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vk_screen_add_surface(vwm->screen);
        vwm->decks[1] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[1], true);
        vk_screen_attach_widget(vwm->screen, 1, VK_WIDGET(vwm->decks[1]));
        vk_object_register_event(VK_OBJECT(vwm->decks[1]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vk_screen_add_surface(vwm->screen);
        vwm->decks[2] = vk_deck_create();
        vk_deck_set_shadow(vwm->decks[2], true);
        vk_screen_attach_widget(vwm->screen, 2, VK_WIDGET(vwm->decks[2]));
        vk_object_register_event(VK_OBJECT(vwm->decks[2]),
            VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);

        vwm->deck = vwm->decks[0];

        vk_object_register_event(VK_OBJECT(vwm->screen),
            VK_EVENT_ON_SURFACE_CHANGE, vwm_on_surface_change, NULL);

        vk_object_register_event(VK_OBJECT(vwm->screen),
            VK_EVENT_ON_TELEPORT, vwm_on_teleport, NULL);

        INIT_LIST_HEAD(&vwm->module_list);

        vwm->hotkey_menu = VWM_HOTKEY_MENU;
        vwm->hotkey_wm = VWM_HOTKEY_WM;
        vwm->hotkey_close = 17;
        vwm->hotkey_cycle = KEY_TAB;
        vwm->hotkey_move_up = KEY_UP;
        vwm->hotkey_move_down = KEY_DOWN;
        vwm->hotkey_move_left = KEY_LEFT;
        vwm->hotkey_move_right = KEY_RIGHT;
        vwm->hotkey_grow_h = '+';
        vwm->hotkey_shrink_h = '-';
        vwm->hotkey_grow_w = '>';
        vwm->hotkey_shrink_w = '<';
        vwm->hotkey_desktop = (27 | (100 << 8));
        {
            const char *term = getenv("TERM");
            if(term != NULL && strcmp(term, "linux") == 0)
            {
                vwm->show_cursor = true;
                vk_screen_set_overlay(vwm->screen, vwm_cursor_overlay);
            }
        }

        // load user profile
        vwm_profile_init(vwm);
    }

	return vwm;
}

void
vwm_input_rearm(vwm_t *vwm)
{
    if(vwm == NULL) return;

    /* re-emit the mouse enable escapes and restore non-blocking input
       against the current tty.  kmio writes the escapes straight to the
       fd, so they have to be resent whenever that fd's terminal may have
       changed: at startup, after teleport (a new fd), and on a dtach
       reattach (a new outer terminal, possibly after `reset`).  On an
       ordinary resize it is a harmless no-op. */
    vk_kmio_init(vk_screen_get_fd(vwm->screen), VWM_KMIO_FLAGS);
    nodelay(stdscr, TRUE);
}

void
vwm_apply_surface_count(int new_count)
{
    vwm_t   *vwm = vwm_get_instance();
    int     old_count = vwm->surface_count;
    int     i;

    if(new_count < 2) new_count = 2;
    if(new_count > 6) new_count = 6;
    if(new_count == old_count) return;

    if(new_count > old_count)
    {
        vwm->decks = realloc(vwm->decks, new_count * sizeof(vk_deck_t *));

        for(i = old_count; i < new_count; i++)
        {
            vk_screen_add_surface(vwm->screen);
            vwm->decks[i] = vk_deck_create();
            vk_deck_set_shadow(vwm->decks[i], true);
            vk_screen_attach_widget(vwm->screen, i,
                VK_WIDGET(vwm->decks[i]));
            vk_object_register_event(VK_OBJECT(vwm->decks[i]),
                VK_EVENT_ON_FINALIZE, vwm_on_deck_finalize, NULL);
        }

        vwm->surface_count = new_count;

        /* push bkgd onto the freshly-created surfaces */
        for(i = old_count; i < new_count; i++)
            vwm_apply_desktop_bkgd(i);

        return;
    }

    int target = new_count - 1;

    for(i = old_count - 1; i >= new_count; i--)
    {
        while(vk_deck_get_widget(vwm->decks[i], 0) != NULL)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->decks[i], 0);
            vk_deck_remove_widget(vwm->decks[i], w);
            vk_deck_add_widget(vwm->decks[target], w, VK_DECK_BOTTOM);
        }

        vk_screen_detach_widget(vwm->screen, i,
            VK_WIDGET(vwm->decks[i]));
        vk_deck_destroy(vwm->decks[i]);
        vk_screen_del_surface(vwm->screen, i);

        /* release the cached wallpaper for the surface we just dropped */
        vwm_invalidate_wallpaper_cache(i);
    }

    vwm->decks = realloc(vwm->decks, new_count * sizeof(vk_deck_t *));
    vwm->surface_count = new_count;

    if(vk_screen_get_active_surface(vwm->screen) >= new_count)
        vk_screen_set_surface(vwm->screen, target);

    vwm->deck = vwm->decks[vk_screen_get_active_surface(vwm->screen)];
}

static int
vwm_on_surface_change(vk_object_t *object, int event, void *anything)
{
    vwm_t       *vwm;
    VWM_PANEL   *panel;
    int         old_surface;
    int         new_surface;

    (void)object;
    (void)event;
    (void)anything;

    vwm = vwm_get_instance();
    panel = vwm_panel_get_data();

    new_surface = vk_screen_get_active_surface(vwm->screen);

    for(old_surface = 0; old_surface < vwm->surface_count; old_surface++)
    {
        if(old_surface == new_surface) continue;

        vk_screen_detach_widget(vwm->screen, old_surface,
            VK_WIDGET(panel->box));
        vk_screen_detach_widget(vwm->screen, old_surface,
            VK_WIDGET(panel->status_box));
    }

    vk_screen_attach_widget(vwm->screen, new_surface,
        VK_WIDGET(panel->box));
    vk_screen_attach_widget(vwm->screen, new_surface,
        VK_WIDGET(panel->status_box));

    vwm->deck = vwm->decks[new_surface];

    /*
        re-decorate every window on the now-active deck so exactly its
        top shows focused.  windows can carry a stale focus border after
        being moved between desktops (Manage Desktop) without a redraw,
        which otherwise leaves two windows looking focused on the
        destination desktop.
    */
    {
        int i, n = vk_deck_count(vwm->deck);
        for(i = 0; i < n; i++)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->deck, i);
            if(w != NULL) vk_window_update(VK_WINDOW(w));
        }
    }

    /* the "(N) Minimized" count is per-desktop -- recount for the new deck */
    vwm_minimized_refresh();

    return 0;
}

static void
vwm_cursor_overlay(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    vwm_t   *vwm = vwm_get_instance();
    int     colors;

    (void)screen;
    (void)surface_id;

    if(!vwm->show_cursor) return;

    colors = COLOR_PAIR(vdk_color_pair(COLOR_YELLOW, COLOR_YELLOW));
    mvwaddch(canvas, vwm->cursor_y, vwm->cursor_x, ' ' | colors);
}

/*
    teleport gives us a brand-new ncurses SCREEN -- color pairs, mouse
    mask, and the non-blocking flag we set at startup all live in the
    old SCREEN.  Re-arm them on the new one, then trigger the existing
    resize cascade so the panel / status bar / open dialogs match the
    new geometry.
*/
static int
vwm_on_teleport(vk_object_t *object, int event, void *anything)
{
    vwm_t   *vwm;

    (void)object;
    (void)event;
    (void)anything;

    vwm = vwm_get_instance();
    if(vwm == NULL) return 0;

    vdk_color_init();

    /* the wallpaper-cache WINDOWs belong to the OLD SCREEN that's about
       to be torn down.  delwin across a different SCREEN corrupts
       ncurses state (same reason libviper leaks the surface canvases on
       teleport), so null the slots without freeing -- next refresh in
       the new SCREEN lazily allocates fresh caches. */
    vwm_invalidate_wallpaper_cache_all_orphan();

    /* re-arm everything kmio set up at startup against the new SCREEN:
       mousemask + mouseinterval are SCREEN-local ncurses state, and
       the \033[?1003h hover escape has to land on the new fd (kmio
       writes it directly to whatever fd we hand it) */
    vwm_input_rearm(vwm);

    /* queue a KEY_RESIZE so the poll loop runs the same cascade it does
       for a real terminal resize (panel + status bar + dialogs) */
    ungetch(KEY_RESIZE);

    return 0;
}
