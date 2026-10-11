#ifndef _VWMTERM_H_
#define _VWMTERM_H_

#include <inttypes.h>

#include <vdk.h>
#include <vtty.h>

#include <ncursesw/curses.h>

#include "../../modules.h"
#include "../../vwm.h"

struct _vwmterm_data_s
{
    vk_window_t     *window;
    vtty_t          *vterm;
    vk_scroller_t   *scroller;
    char            app_title[64];  /* the app's own name, a space each side */
    char            osc_title[128]; /* what the program asked for (OSC 0 / 2),
                                       cleaned of control characters; "" when
                                       it has set none or has given it back */
    char            title[256];     /* the title the window shows outside
                                       selection mode: the app's name, then
                                       the program's title if there is room */
    unsigned int    state;
    int             redraw_pending;
    int             scroll_offset;
    int             frozen;
    int             wake_fd;        /* pty registered with the scheduler, or -1 */
    void            *sched_ctx;     /* this terminal's task (vwm_sched_ctx_t) */
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
