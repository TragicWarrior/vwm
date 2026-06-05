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
#include <signal.h>
#include <time.h>

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
    vwm_panel_init(vwm);

    vk_screen_refresh(vwm->screen);

    vwm_modules_preload(vwm);
    vwm_menubar_init();
    vwm_settings_load(vwm);
    vwm_programs_load(vwm);

    vk_screen_refresh(vwm->screen);

    vwm_sched_run(sched, &shutdown);

    vwm_sched_deinit(sched);

    vk_kmio_shutdown();
    vk_screen_destroy(vwm->screen);
    fsync(fd);
	close(fd);

	return 0;
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
        vk_kmio_init(VK_KMIO_MOUSE | VK_KMIO_MOUSE_HOVER | VK_KMIO_GPM_SIGIO);
        nodelay(stdscr, TRUE);

        vk_screen_set_wallpaper(vwm->screen, vwm_bkgd_simple_normal);

        vwm->deck = vk_deck_create();
        vk_deck_set_shadow(vwm->deck, true);
        vk_screen_attach_widget(vwm->screen, 0, VK_WIDGET(vwm->deck));

        INIT_LIST_HEAD(&vwm->module_list);

        vwm->hotkey_menu = VWM_HOTKEY_MENU;
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
