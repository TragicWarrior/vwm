#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vdk.h>
#include <ncursesw/curses.h>

#include "vwm.h"
#include "private.h"
#include "modules.h"
#include "strings.h"
#include "screensaver.h"

/*
    The screensaver runs a user-configured program in a fullscreen vterm.
    It is attached to the active vk_surface_t as the last widget, so it
    blits over the deck and the panel.  While it runs, poll_input_thd
    forwards keys to its terminal and swallows everything else, so no
    hotkeys or mouse actions reach the window manager until the program
    exits.
*/

static bool             s_active = false;
static vk_window_t      *s_window = NULL;
static int              s_surface = -1;
static time_t           s_last_activity = 0;
static bool             s_cursor_suppressed = false;

void
vwm_screensaver_note_activity(void)
{
    s_last_activity = time(NULL);
}

bool
vwm_screensaver_is_active(void)
{
    return s_active;
}

static bool
dialogs_open(vwm_t *vwm)
{
    if(vwm->menu != NULL) return true;
    if(vwm->calendar_popup != NULL) return true;
    if(vwm->manage_apps_popup != NULL) return true;
    if(vwm->manage_hotkeys_popup != NULL) return true;
    if(vwm->manage_settings_popup != NULL) return true;

    return false;
}

/*
    Fires (via VWM_EVENT_ON_CLOSE) when the saver's child process exits and
    vwmterm's thread closes the window.  We detach from the surface here,
    before the core destroys the window, so the surface is not left holding
    a dangling widget.
*/
static int
screensaver_on_close(vk_object_t *object, int event, void *anything)
{
    vwm_t *vwm = vwm_get_instance();

    (void)event;
    (void)anything;

    if(!s_active || s_window == NULL) return 0;
    if((vk_widget_t *)object != VK_WIDGET(s_window)) return 0;

    vk_screen_detach_widget(vwm->screen, s_surface, VK_WIDGET(s_window));

    s_active = false;
    s_window = NULL;
    s_surface = -1;

    if(s_cursor_suppressed)
    {
        vwm->show_cursor = true;
        s_cursor_suppressed = false;
    }

    s_last_activity = time(NULL);

    /* The lock program (e.g. vlock) ran inside a fullscreen vterm while it
       owned the screen, and vwm_screensaver_input swallowed KEY_RESIZE the
       whole time it was up.  So if a dtach reattach happened DURING the lock
       -- the common case, since the saver is usually what's running when you
       vwm-resume -- the KEY_RESIZE that re-arms the terminal (mouse, cursor,
       keypad, full repaint) was dropped, and vwm comes back with a dead
       mouse and a visible cursor until a manual resize.  Now that the saver
       is gone and s_active is false, queue one KEY_RESIZE so poll_input_thd
       runs the resync cascade.  Idempotent -- and a welcome repaint -- even
       when no reattach occurred (the user just unlocked locally). */
    ungetch(KEY_RESIZE);

    return 0;
}

static void
screensaver_start(void)
{
    vwm_t           *vwm = vwm_get_instance();
    vwm_module_t    *base;
    vwm_module_t    *mod;
    vk_window_t     *window;
    char            buf[NAME_MAX];
    char            **args;
    char            *bin;

    base = vwm_module_find_by_name("vterm-fullscreen");
    if(base == NULL) return;

    mod = vwm_module_clone(base);
    if(mod == NULL) return;

    strncpy(buf, vwm->screensaver_cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    args = strsplitv(buf, " ");
    if(args == NULL || args[0] == NULL)
    {
        if(args != NULL) strfreev(args);
        free(mod);
        return;
    }

    bin = args[0];
    vwm_module_configure(mod, bin, args);
    strfreev(args);

    window = vwm_module_exec(mod);
    if(window == NULL)
    {
        free(mod);
        return;
    }

    s_window = window;
    s_surface = vk_screen_get_active_surface(vwm->screen);
    s_active = true;

    /* last blitter on the surface: covers the deck and the panel */
    vk_screen_attach_widget(vwm->screen, s_surface, VK_WIDGET(window));

    vk_object_register_event(VK_OBJECT(window), VWM_EVENT_ON_CLOSE,
        screensaver_on_close, NULL);

    /* hide the GPM fake cursor while locked */
    if(vwm->show_cursor)
    {
        vwm->show_cursor = false;
        s_cursor_suppressed = true;
    }

    vk_widget_move(VK_WIDGET(window), 0, 0);
    vk_window_update(window);
    vk_screen_refresh(vwm->screen);
}

void
vwm_screensaver_activate(void)
{
    vwm_t   *vwm = vwm_get_instance();

    if(s_active) return;
    if(vwm->screensaver_cmd[0] == '\0') return;

    screensaver_start();
}

void
vwm_screensaver_tick(void)
{
    vwm_t   *vwm = vwm_get_instance();
    time_t  now;

    if(s_active) return;
    if(vwm->screensaver_cmd[0] == '\0') return;
    if(vwm->screensaver_timeout <= 0) return;

    now = time(NULL);

    /* keep the timer fresh while the user is in a menu or dialog */
    if(dialogs_open(vwm))
    {
        s_last_activity = now;
        return;
    }

    if(s_last_activity == 0) s_last_activity = now;

    /* screensaver_timeout is in minutes */
    if((now - s_last_activity) >= (time_t)vwm->screensaver_timeout * 60)
        screensaver_start();
}

void
vwm_screensaver_input(int32_t keystroke, MEVENT *mouse_event)
{
    (void)mouse_event;

    if(!s_active || s_window == NULL) return;

    /* no mouse, no resize; everything else goes to the locked terminal */
    if(keystroke == KEY_MOUSE || keystroke == KEY_RESIZE) return;

    vk_object_push_keystroke(VK_OBJECT(s_window), keystroke);
}
