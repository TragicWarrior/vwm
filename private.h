#ifndef _H_VWM_PRIVATE_
#define _H_VWM_PRIVATE_

#include <signal.h>

#include <glib.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#define  SPRITE_ROWS(x)    (sizeof(x)/sizeof(x[0]))
#define  SPRITE_COLS(x)    (sizeof(x[0])/(sizeof(x[0][0])))


struct sigaction* vwm_sigset(int signum,sighandler_t handler);

/* default borders and controls callbacks */
gint     vwm_default_border_agent_focus(WINDOW *window,gpointer anything);
gint     vwm_default_border_agent_unfocus(WINDOW *window,gpointer anything);

/*	default events	*/
gint 	   vwm_hook_wm_start(WINDOW *window,gpointer arg);
gint 	   vwm_hook_wm_stop(WINDOW *window,gpointer arg);

/* helpers  */
void     vwm_modules_preload(void);

#endif
