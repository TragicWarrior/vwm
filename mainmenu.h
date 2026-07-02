#ifndef _H_VWM_MENU_
#define _H_VWM_MENU_

#include <ncursesw/curses.h>

#define VWM_HOTKEY_MENU     (27 | (96 << 8))

void            vwm_menubar_init(void);
int             vwm_menubar_hotkey(void);
void            vwm_menubar_close_dropdown(void);
int             vwm_menubar_ON_KEYSTROKE(int32_t keystroke);
int             vwm_dropdown_mouse(MEVENT *mouse_event);
void            vwm_minimized_refresh(void);

#endif
