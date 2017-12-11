#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <inttypes.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <viper.h>

int     vwmterm_ON_RESIZE(vwnd_t *vwnd, void *anything);
int	    vwmterm_ON_CLOSE(vwnd_t *vwnd, void *anything);
int	    vwmterm_ON_DESTROY(vwnd_t *vwnd, void *anything);
int	    vwmterm_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd);

#endif
