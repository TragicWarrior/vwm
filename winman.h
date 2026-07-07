#ifndef _VWM_WINMAN_H_
#define _VWM_WINMAN_H_

#include <inttypes.h>

#include <ncursesw/curses.h>


#define VWM_HOTKEY_WM       (27 | (119 << 8))

#define  VWM_WM_HELP       \
"Press [Tab] to change window focus. \
Use keys [Up Dn Lt Rt] to move windows.  \
Use keys [+ - < >] to resize windows. \
Press [Ctrl+Q] to close a window."

#define  VWM_WINDOW_HELP   \
"Alt+PgUp/PgDn to scroll, Alt+Shft+V to paste.  \
Press Alt ~ for Menu.  Press Alt+W to manage windows."


void    vwm_default_VWM_START(void);
void    vwm_default_VWM_STOP(void);

int     vwm_on_deck_finalize(vk_object_t *object, int event, void *anything);

void    vwm_default_WINDOW_CYCLE(void);
void    vwm_default_WINDOW_CLOSE(vk_widget_t *widget);
void    vwm_minimize_window(vk_widget_t *widget);
void    vwm_restore_window(vk_widget_t *widget);
void    vwm_fit_window_onscreen(vk_widget_t *widget, int scr_w, int scr_h);

void    vwm_default_WINDOW_MOVE_UP(vk_widget_t *widget);
void    vwm_default_WINDOW_MOVE_DOWN(vk_widget_t *widget);
void    vwm_default_WINDOW_MOVE_LEFT(vk_widget_t *widget);
void    vwm_default_WINDOW_MOVE_RIGHT(vk_widget_t *widget);
void    vwm_default_WINDOW_INCREASE_HEIGHT(vk_widget_t *widget);
void    vwm_default_WINDOW_DECREASE_HEIGHT(vk_widget_t *widget);
void    vwm_default_WINDOW_INCREASE_WIDTH(vk_widget_t *widget);
void    vwm_default_WINDOW_DECREASE_WIDTH(vk_widget_t *widget);


#endif
