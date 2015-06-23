#ifndef _H_VWM_MENU_
#define _H_VWM_MENU_

#include <glib.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#include <ncursesw/menu.h>
#else
#include <curses.h>
#include <menu.h>
#endif

#define VWM_MAIN_MENU_HELP \
"Press [alt ~] for Main Menu.  Press [alt w] to manage windows."

WINDOW*	vwm_main_menu(void);
gint	   vwm_main_menu_hotkey(void);

/*	viper events	*/
gint 	vwm_main_menu_ON_ACTIVATE(WINDOW *window,gpointer arg);
gint	vwm_main_menu_ON_CLOSE(WINDOW *window,gpointer arg);
gint 	vwm_main_menu_ON_KEYSTROKE(gint32 keystroke,WINDOW *window);

/* helpers */
void  vwm_menu_marshall(MENU *menu,gint32 key_vector);

#endif
