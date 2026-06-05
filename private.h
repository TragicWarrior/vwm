#ifndef _H_VWM_PRIVATE_
#define _H_VWM_PRIVATE_

#include <inttypes.h>
#include <stdbool.h>
#include <signal.h>

#include <ncursesw/curses.h>

#include <libconfig.h>
#include "protothread.h"
#include "sched.h"

#include <vdk.h>
#include "list.h"
#include "profile.h"
#include "vwm.h"

struct _vwm_s
{
    config_t                config;
    vwm_profile_t           *profile;

    int32_t                 hotkey_menu;

    struct list_head        module_list;

    vk_screen_t             *screen;
    vk_deck_t               **decks;
    vk_deck_t               *deck;
    int                     surface_count;
    vk_window_t             *menu;
    vk_menubar_t            *menubar;
    int                     menu_item_idx;

    vk_window_t             *calendar_popup;
    vk_window_t             *manage_apps_popup;

    uint32_t                state;

    int                     cursor_x;
    int                     cursor_y;
    bool                    show_cursor;
};


#define  SPRITE_ROWS(x)    (sizeof(x) / sizeof(x[0]))
#define  SPRITE_COLS(x)    (sizeof(x[0]) / (sizeof(x[0][0])))

struct sigaction* vwm_sigset(int signum, sighandler_t handler);

/* border decoration callback for vk_window_t */
void    vwm_window_decorate(vk_window_t *window, WINDOW *canvas, void *data);

/*	default events	*/
int     vwm_hook_wm_start(WINDOW *window, void *arg);
int 	vwm_hook_wm_stop(WINDOW *window, void *arg);

/* helpers  */
void    vwm_modules_preload(vwm_t *vwm);

int     vwm_exit(vk_widget_t *widget, void *anything);
int     vwm_toggle_winman(vk_widget_t *widget, void *anything);

#endif
