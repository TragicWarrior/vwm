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

#include <viper.h>
#include <protothread.h>

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

/*
   According to GNU libc documentation. sig_atomic_t "is always atomic...
   Reading and writing this data type is guaranteed to happen in a single
   instruction.  The volatile qualifier appears to be implied for C99
   but not necessarily true otherwise.
*/

protothread_t           pt[2];
int                     shutdown = 0;

unsigned int            clock_tick = 0;

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

    extern protothread_t    pt[2];
    bool                    pt_status[2];
    unsigned int            pt_selector;
    pt_context_t            *ctx_clock;
    pt_context_t            *ctx_poll_input;
    clock_data_t            *clock_data;
    MEVENT                  mouse_event;

    pt[PT_PRIORITY_NORMAL] = protothread_create();
    pt[PT_PRIORITY_HIGH] = protothread_create();

    // setup protothread for clock
    ctx_clock = malloc(sizeof(pt_context_t));
    clock_data = vwm_clock_init();
    ctx_clock->shutdown = &shutdown;
    ctx_clock->anything = (void *)clock_data;

    // setup prototrhead for input polling
    ctx_poll_input = malloc(sizeof(pt_context_t));
    ctx_poll_input->anything = &mouse_event;
    ctx_poll_input->shutdown = &shutdown;

    pt_create(pt[PT_PRIORITY_NORMAL], &ctx_clock->pt_thread,
                vwm_clock_driver, ctx_clock);
    pt_create(pt[PT_PRIORITY_HIGH], &ctx_poll_input->pt_thread,
                vwm_poll_input, ctx_poll_input);

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

    viper_init(VIPER_GPM_SIGIO);
    viper_set_border_agent(vwm_default_border_agent_unfocus, 0);
    viper_set_border_agent(vwm_default_border_agent_focus, 1);

	// use the integrated window manager
	vwm = vwm_init();
    vwm_panel_init();

    // set hook to trap and filter keystrokes for window-management
    // viper_kmio_dispatch_set_hook(KMIO_HOOK_ENTER,
        // vwm_kmio_dispatch_hook_enter);

    viper_screen_redraw(0, REDRAW_BACKGROUND);
	viper_screen_redraw(0, REDRAW_ALL);

    vwm_modules_preload(vwm);
    vwm_settings_load(vwm);
    vwm_programs_load(vwm);

    vwm_panel_message_add(VWM_MAIN_MENU_HELP,-1);

    // protothread driver
    do
    {
        pt_selector = PT_PRIORITY_NORMAL;
        pt_status[pt_selector] = protothread_run(pt[pt_selector]);

        pt_selector = PT_PRIORITY_HIGH;
        pt_status[pt_selector] = protothread_run(pt[pt_selector]);
    }
    while(pt_status[PT_PRIORITY_NORMAL] && pt_status[PT_PRIORITY_HIGH]);

    viper_end();
    fsync(fd);
	close(fd);

	return 0;
}

vwm_t*
vwm_init(void)
{
	static vwm_t    *vwm = NULL;
    int             width;
    int             height;
    int             screen_id;

	if(vwm == NULL)
	{
        screen_id = CURRENT_SCREEN_ID;
        getmaxyx(CURRENT_SCREEN, height, width);

 		vwm = (vwm_t*)calloc(1, sizeof(vwm_t));

        vwm->wallpaper[screen_id] = newwin(height, width, 0, 0);

        viper_screen_set_wallpaper(screen_id, vwm->wallpaper[screen_id],
            vwm_bkgd_simple_normal);

        INIT_LIST_HEAD(&vwm->module_list);

        vwm->hotkey_menu = VWM_HOTKEY_MENU;
        vwm->hotkey_menu_msg = VWM_MAIN_MENU_HELP;

        // load user profile
        vwm_profile_init(vwm);

        // todo:  move more init stuff here
    }

	return vwm;
}

