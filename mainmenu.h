#ifndef _H_VWM_MENU_
#define _H_VWM_MENU_

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#include <ncursesw/menu.h>
#else
#include <curses.h>
#include <menu.h>
#endif

#define VWM_HOTKEY_MENU     (27 | (96 << 8))

#define VWM_MAIN_MENU_HELP \
"Press [alt ~] for Main Menu"

vwnd_t* vwm_main_menu(void);
int	    vwm_main_menu_hotkey(void);

/*	viper events	*/
int 	vwm_main_menu_ON_CLOSE(vwnd_t *vwnd, void *arg);

int 	vwm_main_menu_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd);

#endif
