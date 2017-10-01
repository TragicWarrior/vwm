#ifndef _H_VWM_PRIVATE_
#define _H_VWM_PRIVATE_

#include <signal.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <protothread.h>

#define  SPRITE_ROWS(x)    (sizeof(x) / sizeof(x[0]))
#define  SPRITE_COLS(x)    (sizeof(x[0]) / (sizeof(x[0][0])))

struct sigaction* vwm_sigset(int signum, sighandler_t handler);

/* default borders and controls callbacks */
int     vwm_default_border_agent_focus(WINDOW *window, void *anything);
int     vwm_default_border_agent_unfocus(WINDOW *window, void *anything);

/*	default events	*/
int     vwm_hook_wm_start(WINDOW *window, void *arg);
int 	vwm_hook_wm_stop(WINDOW *window, void *arg);

/* helpers  */
void     vwm_modules_preload(void);

#endif
