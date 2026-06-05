#ifndef _H_VWM_MENU_
#define _H_VWM_MENU_

#include <ncursesw/curses.h>

#define VWM_HOTKEY_MENU     (27 | (96 << 8))

#define VWM_MENU_HELP \
" ☰ Menu (Alt ~)"

#define VWM_MENU_HELP_ASCII \
" [=] Menu (Alt ~)"

void            vwm_menubar_init(void);
int             vwm_menubar_hotkey(void);
void            vwm_menubar_close_dropdown(void);
int             vwm_menubar_ON_KEYSTROKE(int32_t keystroke);
int             vwm_dropdown_mouse(MEVENT *mouse_event);

#endif
