#ifndef _H_VWM_PRIVATE_
#define _H_VWM_PRIVATE_

#include <inttypes.h>
#include <signal.h>

#include <ncursesw/curses.h>

#include <libconfig.h>
#include <protothread.h>

#include "viper.h"
#include "list.h"
#include "profile.h"
#include "vwm.h"

enum
{
    PT_PRIORITY_NORMAL      =   0x00,
    PT_PRIORITY_HIGH        =   0x01
};

typedef struct
{
    pt_thread_t             pt_thread;
    pt_func_t               pt_func;

    // data shared by all protothreads
    int                     *shutdown;
    void                    *anything;
}
pt_context_t;

struct _vwm_s
{
    config_t                config;
    vwm_profile_t           *profile;

    int32_t                 hotkey_menu;
    char                    *hotkey_menu_msg;

    struct list_head        module_list;

    WINDOW                  *wallpaper[4];
    int                     (*wallpaper_agent)  (WINDOW *, void *);

    uint32_t                state;
};


#define  SPRITE_ROWS(x)    (sizeof(x) / sizeof(x[0]))
#define  SPRITE_COLS(x)    (sizeof(x[0]) / (sizeof(x[0][0])))

struct sigaction* vwm_sigset(int signum, sighandler_t handler);

/* default borders and controls callbacks */
int     vwm_default_border_agent_focus(vwnd_t *vwnd, void *anything);
int     vwm_default_border_agent_unfocus(vwnd_t *vwnd, void *anything);

/*	default events	*/
int     vwm_hook_wm_start(WINDOW *window, void *arg);
int 	vwm_hook_wm_stop(WINDOW *window, void *arg);

/* helpers  */
void    vwm_modules_preload(vwm_t *vwm);

int     vwm_exit(vk_widget_t *widget, void *anything);
int     vwm_toggle_winman(vk_widget_t *widget, void *anything);

#endif
