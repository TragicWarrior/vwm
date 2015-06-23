#ifndef _H_VWM_WINLIST_
#define _H_VWM_WINLIST_

#include <glib.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

WINDOW*	vwm_fmod_wndlist(gpointer anything);

gint	vwm_fmod_wndlist_ON_DESTROY(WINDOW *window,gpointer arg);
gint	vwm_fmod_wndlist_ON_ACTIVATE(WINDOW *window,gpointer arg);
gint	vwm_fmod_wndlist_ON_KEYSTROKE(gint32 keystroke,WINDOW *window);

#endif
