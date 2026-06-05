#ifndef _H_VWM_PANEL_
#define _H_VWM_PANEL_

#include <inttypes.h>

#include <ncursesw/curses.h>

#include <vdk.h>

#include "list.h"

#define  VWM_PANEL_STATE_FROZEN  (1<<1)

#define  VWM_PANEL_MSG_TTL_MAX   30

typedef struct
{
    struct list_head    msg_list;
    int8_t              msg_count;

    vk_box_t            *box;
    vk_menubar_t        *menubar;
    vk_label_t          *msg_label;
    const char          *msg_default;
    vk_label_t          *task_label;
    vk_label_t          *clock_label;
    vk_activity_t       *activity;

    vk_label_t          *desktop_prompt;

    vk_box_t            *status_box;
    vk_marquee_t        *status_marquee;
    vk_label_t          *version_label;

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
void    vwm_panel_ON_TERM_RESIZED(VWM_PANEL *panel);
void    vwm_panel_ON_CLOCK_TICK(VWM_PANEL *panel);
int     vwm_panel_ON_KEYSTROKE(int32_t keystroke, void *anything);

/* panel data access */
VWM_PANEL*  vwm_panel_get_data(void);

/* helpers  */
void    vwm_panel_update_throbber(VWM_PANEL *panel);
void    vwm_panel_update_taskcount(VWM_PANEL *panel);
void    vwm_panel_update_clock(VWM_PANEL *panel);
void    vwm_panel_display(VWM_PANEL *panel);
void    vwm_panel_set_status(const char *text);

void    vwm_desktop_prompt_show(void);

void    vwm_calendar_toggle(void);
void    vwm_calendar_close(void);

#endif
