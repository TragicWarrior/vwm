#ifndef _VWM_WINMAN_H_
#define _VMW_WINMAN_H_

#include <inttypes.h>

#include <ncursesw/curses.h>


#define VWM_HOTKEY_WM       (27 | (119 << 8))

#define  VWM_WM_HELP       \
"Press [Tab] to change window focus. \
Use keys [Up Dn Lt Rt] to move windows.  \
Use keys [+ - < >] to resize windows. \
Press [Ctrl+Q] to close a window."


void    vwm_default_VWM_START(vwnd_t *topmost_window);
void    vwm_default_VWM_STOP(vwnd_t *topmost_window);

void    vwm_default_WINDOW_CYCLE(void);
void    vwm_default_WINDOW_CLOSE(vwnd_t *topmost_window);

void    vwm_default_WINDOW_MOVE_UP(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_DOWN(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_LEFT(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_RIGHT(vwnd_t *topmost_window);
void    vwm_default_WINDOW_INCREASE_HEIGHT(vwnd_t *);
void    vwm_default_WINDOW_DECREASE_HEIGHT(vwnd_t *);
void    vwm_default_WINDOW_INCREASE_WIDTH(vwnd_t *);
void    vwm_default_WINDOW_DECREASE_WIDTH(vwnd_t *);


#endif
