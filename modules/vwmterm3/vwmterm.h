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

// extend the standard vwm module
struct _vwmterm_mod_s
{
    vwm_module_t    module;

    uint32_t        flags;
    char            **exec_args;
};

struct _vwmterm_data_s
{
    vwnd_t          *vwnd;
    vterm_t         *vterm;
    unsigned int    state;
};

enum
{
    VWMTERM_STATE_EXITING   =   0x0,
    VWMTERM_STATE_RUNNING,
    VWMTERM_STATE_EPIPE
};

typedef struct _vwmterm_data_s  vwmterm_data_t;
typedef struct _vwmterm_mod_s   vwmterm_mod_t;

#endif
