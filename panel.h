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

/* put the status bar back to what it shows when nothing else has a
   claim on it: the window key help when the current desktop has a
   window, the menu hint when it is empty.  every dialog calls this as
   it closes. */
void    vwm_panel_status_idle(void);

/* re-pick the panel's UTF-8 / ASCII glyphs for the terminal the session
   is on now.  call after the terminal type changed (an adopt). */
void    vwm_panel_refresh_glyphs(void);

void    vwm_desktop_prompt_show(void);


void    vwm_calendar_toggle(void);
void    vwm_calendar_close(void);

#endif
