#ifndef _VWMTERM_H_
#define _VWMTERM_H_

#include <inttypes.h>

#include <viper.h>
#include <vterm.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <viper.h>

#include "../../modules.h"
#include "../../vwm.h"

struct _vwmterm_data_s
{
    vwnd_t          *vwnd;
    vterm_t         *vterm;
    unsigned int    state;
    int             redraw_pending;
    int             scroll_offset;
    int             frozen;
    int             sel_anchor_row;
    int             sel_anchor_col;
    int             sel_end_row;
    int             sel_end_col;
};

enum
{
    VWMTERM_STATE_EXITING   =   0x0,
    VWMTERM_STATE_RUNNING,
    VWMTERM_STATE_EPIPE
};

typedef struct _vwmterm_data_s  vwmterm_data_t;

#endif
