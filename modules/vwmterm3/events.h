#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <glib.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

gint  vwmterm_ON_RESIZE(WINDOW *window,gpointer anything);
gint	vwmterm_ON_CLOSE(WINDOW *window,gpointer anything);
gint	vwmterm_ON_DESTROY(WINDOW *window,gpointer anything);
gint	vwmterm_ON_KEYSTROKE(gint32 keystroke,WINDOW *window);

#endif
