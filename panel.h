#ifndef _H_VWM_PANEL_
#define _H_VWM_PANEL_

#include <inttypes.h>

#include <ncursesw/curses.h>

#include <vdk.h>

#include "list.h"

typedef struct
{
    vk_box_t            *box;
    vk_menubar_t        *menubar;
    vk_label_t          *task_label;
    vk_label_t          *clock_label;
    vk_activity_t       *activity;

    vk_label_t          *desktop_prompt;

    vk_label_t          *teleport_prompt;
    char                teleport_text[128];
    int                 teleport_pos;

    vk_box_t            *status_box;
    vk_marquee_t        *status_marquee;
    vk_label_t          *version_label;

    int32_t             clock;
}
VWM_PANEL;

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
void    vwm_panel_set_status(const char *text);

void    vwm_desktop_prompt_show(void);

void    vwm_teleport_prompt_show(void);

void    vwm_calendar_toggle(void);
void    vwm_calendar_close(void);

#endif
