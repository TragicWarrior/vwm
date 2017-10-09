#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <inttypes.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

int     vwmterm_ON_RESIZE(WINDOW *window, void *anything);
int	    vwmterm_ON_CLOSE(WINDOW *window, void *anything);
int	    vwmterm_ON_DESTROY(WINDOW *window, void *anything);
int	    vwmterm_ON_KEYSTROKE(int32_t keystroke, WINDOW *window);

#endif
