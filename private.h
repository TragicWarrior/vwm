#ifndef _H_VWM_PRIVATE_
#define _H_VWM_PRIVATE_

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <signal.h>

#include <ncursesw/curses.h>

#include "protothread.h"
#include "sched.h"

#include <vdk.h>
#include "list.h"
#include "profile.h"
#include "vwm.h"

/* matches the 2..6 desktop-count range enforced by the Settings dialog */
#define VWM_MAX_DESKTOPS    6

struct _vwm_s
{
    vwm_profile_t           *profile;

    int32_t                 hotkey_menu;
    int32_t                 hotkey_wm;
    int32_t                 hotkey_close;
    int32_t                 hotkey_cycle;
    int32_t                 hotkey_move_up;
    int32_t                 hotkey_move_down;
    int32_t                 hotkey_move_left;
    int32_t                 hotkey_move_right;
    int32_t                 hotkey_grow_h;
    int32_t                 hotkey_shrink_h;
    int32_t                 hotkey_grow_w;
    int32_t                 hotkey_shrink_w;
    int32_t                 hotkey_desktop;

    struct list_head        module_list;

    vk_screen_t             *screen;
    vk_deck_t               **decks;
    vk_deck_t               *deck;
    int                     surface_count;

    /* coalesced-refresh flag.  vterm drain tasks set this instead of
       compositing per tile; the scheduler's per-step hook
       (vwm_sched_render in vwm.c) issues one vk_screen_refresh per step
       and clears it.  see modules/vwmterm3/pt_thread.c. */
    int                     screen_dirty;

    vk_window_t             *menu;
    vk_menubar_t            *menubar;
    int                     menu_item_idx;

    vk_window_t             *calendar_popup;
    vk_window_t             *manage_apps_popup;
    vk_window_t             *manage_hotkeys_popup;
    vk_window_t             *manage_settings_popup;

    /* a surface-attached modal system tool (e.g. the print dialog);
       poll_input routes all input here while it is set */
    vk_window_t             *tool_window;

    char                    task_indicator_action[NAME_MAX];
    char                    date_click_action[NAME_MAX];

    char                    screensaver_cmd[NAME_MAX];
    int                     screensaver_timeout;        /* idle minutes; 0=off */

    /* per-surface ANSI color (0..15).  index by surface id; up to
       VWM_MAX_DESKTOPS entries -- only [0..surface_count-1] are live.
       desktop_color is the wallpaper background, desktop_fg the glyph
       (pattern) foreground. */
    short                   desktop_color[VWM_MAX_DESKTOPS];
    short                   desktop_fg[VWM_MAX_DESKTOPS];

    /* per-surface wallpaper pattern (VWM_WALLPAPER_*) */
    short                   desktop_wallpaper[VWM_MAX_DESKTOPS];

    /* VWM_CLIPBOARD_* -- how SELECT-mode copy reaches the host clipboard */
    short                   clipboard_mode;

    /* bottom-left hostname label (Settings: Show Hostname + Hostname
       Colors).  show_hostname 0 = off (default), 1 = on. */
    short                   show_hostname;
    short                   hostname_fg;
    short                   hostname_bg;

    /* big-font hostname via the vwmfont module.  hostname_font: -1 =
       Basic (plain one-row label, default), else a vwmfont size index.
       hostname_fill: 0 = full block, 1 = 'O', 2 = 'X'. */
    short                   hostname_font;
    short                   hostname_fill;

    uint32_t                state;

    /* window whose border is flashing to request user attention
       (vwm-msg attention).  NULL = none.  attention_phase toggles
       every VWM_ATTENTION_TICKS clock ticks (1/2 s). */
    vk_widget_t             *attention;
    int                     attention_phase;
    int                     attention_hold;

    int                     cursor_x;
    int                     cursor_y;
    bool                    show_cursor;
};


#define  SPRITE_ROWS(x)    (sizeof(x) / sizeof(x[0]))
#define  SPRITE_COLS(x)    (sizeof(x[0]) / (sizeof(x[0][0])))

void vwm_sigset(int signum, sighandler_t handler);

/* border decoration callback for vk_window_t */
void    vwm_window_decorate(vk_window_t *window, WINDOW *canvas, void *data);

/* helpers  */
void    vwm_modules_preload(vwm_t *vwm);

int     vwm_exit(vk_widget_t *widget, void *anything);
int     vwm_toggle_winman(vk_widget_t *widget, void *anything);

#endif
