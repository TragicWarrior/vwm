#ifndef _VWM_HOTKEYS_H_
#define _VMW_HOTKEYS_H_

#include <inttypes.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#define VWM_HOTKEY_MENU     (27 | (96 << 8))
#define VWM_HOTKEY_WM       (27 | (119 << 8))

#define  VWM_WM_HELP       \
"Press [Tab] to change window focus. \
Use keys [Up Dn Lt Rt] to move windows.  \
Use keys [+ - < >] to resize windows. \
Press [Ctrl+Q] to close a window."

int32_t vwm_kmio_dispatch_hook_enter(int32_t keystroke);

void    vwm_default_VWM_START(vwnd_t *topmost_window);
void    vwm_default_VWM_STOP(vwnd_t *topmost_window);

void    vwm_default_WINDOW_CYCLE(void);

void    vwm_default_WINDOW_CLOSE(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_UP(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_DOWN(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_LEFT(vwnd_t *topmost_window);
void    vwm_default_WINDOW_MOVE_RIGHT(vwnd_t *topmost_window);

#endif
