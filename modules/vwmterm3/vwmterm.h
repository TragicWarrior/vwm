#ifndef _VWMTERM_H_
#define _VWMTERM_H_

#include <glib.h>

#include <viper.h>
#include <vterm.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

struct _vwmterm_data_s
{
    WINDOW          *window;
    vterm_t         *vterm;
    unsigned int    state;
};

enum
{
    VWMTERM_STATE_EXITING   =   0x0,
    VWMTERM_STATE_RUNNING
};

typedef struct _vwmterm_data_s  vwmterm_data_t;

#endif
