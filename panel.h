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
    vk_widget_t         *desktops;      /* " 1  2  3 ", drawn by the panel */
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
void    vwm_panel_update_desktops(VWM_PANEL *panel);
int     vwm_panel_desktop_at(VWM_PANEL *panel, int x);
void    vwm_panel_set_status(const char *text);

/* redraw the menu bar and copy it onto the top bar.  the bar is what
   the screen composites; redrawing the menu bar widget alone does not
   change that copy. */
void    vwm_panel_sync_menubar(void);

/* rebuild both bars from current widget state.  used when a detached
   session attaches: the tick advanced the clock and the marquee
   without copying them onto the bars. */
void    vwm_panel_rebuild_bars(void);

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
