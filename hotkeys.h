#ifndef _VWM_HOTKEYS_H_
#define _VMW_HOTKEYS_H_

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <glib.h>

#define  VWM_HOTKEY_MENU   (27 | (96<<8))
#define  VWM_HOTKEY_WM     (27 | (119<<8))

#define  VWM_WM_HELP       \
"Press [Tab] to change window focus. \
Use the keys [Up/Dn/Lt/Rt] to move windows.  \
Press [Ctrl+Q] to close a window."

gint32   vwm_kmio_dispatch_hook_enter(gint32 keystroke);

void     vwm_default_VWM_START(WINDOW *topmost_window);
void     vwm_default_VWM_STOP(WINDOW *topmost_window);

void     vwm_default_WINDOW_CYCLE(void);

void     vwm_default_WINDOW_CLOSE(WINDOW *topmost_window);
void     vwm_default_WINDOW_MOVE_UP(WINDOW *topmost_window);
void     vwm_default_WINDOW_MOVE_DOWN(WINDOW *topmost_window);
void     vwm_default_WINDOW_MOVE_LEFT(WINDOW *topmost_window);
void     vwm_default_WINDOW_MOVE_RIGHT(WINDOW *topmost_window);

#endif
