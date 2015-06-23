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

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __linux
#include <sys/klog.h>
#endif

#include <glib.h>
#include <gmodule.h>

//#include <menu.h>
#include <pseudo.h>
#include <viper.h>

#include "vwm.h"
#include "private.h"
#include "bkgd.h"
#include "mainmenu.h"
#include "vwm_panel.h"
#include "modules.h"
#include "vwm_psthread.h"
#include "settings.h"
#include "signals.h"
#include "hotkeys.h"

ps_runq_t    *vwm_runq = NULL;

/*
   According to GNU libc documentation. sig_atomic_t "is always atomic...
   Reading and writing this data type is guaranteed to happen in a single
   instruction.  The volatile qualifier appears to be implied for C99 
   but not necessarily true otherwise.
*/

volatile sig_atomic_t	vwm_task_count=0;

// store argv and argc for use elsewhere (with modules)
char    **vwm_argv;
int     vwm_argc;

int main(int argc,char **argv)
{
	extern ps_runq_t        *vwm_runq;
    extern char             **vwm_argv;
    extern int              vwm_argc;
	struct timespec  		sleep_value = {.tv_sec = 0,.tv_nsec = 1000};
	gint					ui_speed;
	gchar		      		*msg;
	gint		      		fd;
	gchar		      		*locale=NULL;
	int						flags;

    vwm_argc = argc;
    vwm_argv = argv;

	/*	set the locale to the default settings (as configured by env).
		this is required for ncurses to work properly.	*/	
	locale = setlocale(LC_ALL,"");

	// print some debug information
	printf("%s\n\r",locale);
	printf("ncurses = %d.%d (%d)\n\r",NCURSES_VERSION_MAJOR,
		NCURSES_VERSION_MINOR,NCURSES_VERSION_PATCH);
	fflush(NULL);

#ifdef __linux
    // suppress printk messages.  klogctl() is linux specific.
	klogctl(6,NULL,0);
    printf("VWM running on Linux\n\r");
#endif

    // supress STDERR
	fd = open("/dev/null",O_WRONLY);
	if(fd == -1) exit(0);
	dup2(fd,STDERR_FILENO);

	// ignore terminal interrupt signal
    vwm_sigset(SIGINT,SIG_IGN);

#ifdef _DEBUG
    vwm_sigset(SIGILL,vwm_backtrace);
    vwm_sigset(SIGSEGV,vwm_backtrace);
    vwm_sigset(SIGFPE,vwm_backtrace);
#endif

	vwm_sigset(SIGIO,vwm_SIGIO);
	fcntl(STDIN_FILENO,F_SETOWN,getpid());
	flags = fcntl(STDIN_FILENO,F_GETFL);
	fcntl(STDIN_FILENO,F_SETFL, flags | FASYNC);

	// start thread system
	g_thread_init(NULL);

    viper_init(VIPER_GPM_SIGIO);
    viper_set_border_agent(vwm_default_border_agent_unfocus,0);
    viper_set_border_agent(vwm_default_border_agent_focus,1);

	// use the integrated window manager
	vwm_init();
    vwm_panel_init();

    // set hook to trap and filter keystrokes for window-management
    viper_kmio_dispatch_set_hook(KMIO_HOOK_ENTER,
        vwm_kmio_dispatch_hook_enter);

	viper_screen_redraw(REDRAW_ALL);

    vwm_modules_preload();

	vwm_runq = psthread_init(PS_SCHED_LEAPFROG);
	psthread_add(vwm_runq,vwm_clock_driver,NULL);
	psthread_add(vwm_runq,vwm_poll_input,NULL);

/* this will load the default screensaver but it will be immediately
   overridden if by vwm_settings_load() if the user has specified something
   different in their ~/.vwm/vwmrc file.  */

/*
#ifdef _VWM_SCREENSAVER_TIMEOUT
   vwm_scrsaver_timeout_set(_VWM_SCREENSAVER_TIMEOUT);
   vwm_scrsaver_set("SysSaver");
#endif
*/

    vwm_settings_load(VWM_RC_FILE);

// vwm_scrsaver_start();

    vwm_panel_message_add(VWM_MAIN_MENU_HELP,-1);

    while(TRUE)
	{
	    msg = (gchar*)shmq_msg_get("ui");
		if(msg != NULL) if(strcmp(msg,"shutdown vwm") == 0) break;

		ui_speed = vwm_ui_get_speed();
		if(ui_speed > 1)
		{
			psthread_run(vwm_runq,ui_speed);
			vwm_ui_set_speed(1);
		}
		else psthread_run(vwm_runq,1);

		nanosleep(&sleep_value,NULL);
	}

    viper_end();
    fsync(fd);
	close(fd);

	return 0;
}

VWM* vwm_init(void)
{
	static VWM	*vwm=NULL;
   WINDOW      *wallpaper_wnd;

	if(vwm==NULL)
	{
      wallpaper_wnd=viper_screen_get_wallpaper();

 		vwm=(VWM*)g_malloc0(sizeof(VWM));
      vwm->wallpaper_agent=vwm_bkgd_simple;

      // initialize wallpaper
      vwm->wallpaper_agent(wallpaper_wnd,(gpointer)0);
	}

	return vwm;
}

void vwm_ui_accel(gint val)
{
	extern volatile sig_atomic_t	vwm_task_count;

	if(val<1) val=1;

	vwm_task_count+=val;
}

void vwm_ui_slow(gint val)
{
	extern volatile sig_atomic_t  vwm_task_count;

   if(val<1) val=1;

   vwm_task_count-=val;
	if(vwm_task_count<0) vwm_task_count=0;
}

gint vwm_ui_get_speed(void)
{
	extern volatile sig_atomic_t vwm_task_count;

	return (gint)vwm_task_count;
}

void vwm_ui_set_speed(guint speed)
{
	extern volatile sig_atomic_t vwm_task_count;

	vwm_task_count=speed;
}

