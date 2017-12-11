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

struct _vwmterm_data_s
{
    vwnd_t          *vwnd;
    vterm_t         *vterm;
    unsigned int    state;

    uint32_t        flags;
    char            **exec_args;
};

enum
{
    VWMTERM_STATE_EXITING   =   0x0,
    VWMTERM_STATE_RUNNING
};

typedef struct _vwmterm_data_s      vwmterm_data_t;
typedef struct _vwmterm_mod_data_s  vwmterm_mod_data_s;

#endif
