#ifndef _H_VWM_PANEL_
#define _H_VWM_PANEL_

#include <inttypes.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include "list.h"

#define  VWM_PANEL_STATE_FROZEN  (1<<1)

/* maximum seconds a message can live on the panel before needing
   be touched. */
#define  VWM_PANEL_MSG_TTL_MAX   30       

typedef struct
{
    struct list_head    msg_list;
    int8_t              msg_count;
    char                *display;
    int16_t             display_sz;
    char                *pos;
    int32_t             tick_rate;
    int32_t             freeze_time;
    int32_t             thaw_timer;
    int16_t             thaw_rate;
    int32_t             clock;
    uint32_t            state;
}
VWM_PANEL;

typedef struct
{
    union
    {
        void            *msg_addr;
        uintmax_t       msg_handle;
    }
                        msg_id;
    struct list_head    list;
    char                *msg;
    int                 msg_len;
    int32_t             timeout;
    int32_t             touch_val;
}
VWM_PANEL_MSG;

/* panel events   */
int     vwm_panel_ON_TERM_RESIZED(WINDOW *window, void *arg);
int     vwm_panel_ON_CLOCK_TICK(WINDOW *window, void *arg);

/* helpers  */
void	vwm_panel_update_throbber(WINDOW *window);
void    vwm_panel_update_clock(WINDOW *window);
void    vwm_panel_marshall(VWM_PANEL *vwm_panel);
void    vwm_panel_display(VWM_PANEL *vwm_panel, WINDOW *window);
void    vwm_panel_scroll(VWM_PANEL *vwm_panel);

#endif
