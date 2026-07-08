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

    /* While locked, poll_input_thd forwards a KEY_RESIZE only to the saver
       overlay (vwm_screensaver_resize) so the fullscreen lock tracks the new
       geometry; the desktop beneath stays hidden and is NOT repainted.  So once
       the saver is gone, queue one KEY_RESIZE to run the full resync cascade on
       the now-revealed desktop: re-arm mouse/cursor/keypad (a dtach reattach
       may have happened during the lock) and repaint + clamp the windows to the
       current geometry.  Idempotent -- and a welcome repaint -- even when no
       reattach occurred (a local unlock). */
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

/*
    Resize the fullscreen saver overlay to the current screen size after a
    geometry change (e.g. a dtach reattach onto a larger/smaller terminal).
    The saver window is created VK_STATE_NORESIZE so the user can't resize it,
    so clear that just for this programmatic resize.  The content child is
    resized first so the window's VK_EVENT_ON_RESIZE handler (vwmterm) reads the
    NEW interior and hands it to vterm_resize() -- libvterm then TIOCSWINSZ +
    SIGWINCHes the locked program, which repaints itself at the new size.  Only
    the overlay is touched; the hidden desktop below is never repainted, so the
    lock is never broken by a resize.
*/
void
vwm_screensaver_resize(void)
{
    vwm_t       *vwm = vwm_get_instance();
    vk_widget_t *content;
    uint32_t     state;
    int          h, w;

    if(!s_active || s_window == NULL) return;

    getmaxyx(vk_screen_get_window(vwm->screen), h, w);
    if(w < 1 || h < 1) return;

    state = vk_widget_get_state(VK_WIDGET(s_window));
    vk_widget_set_state(VK_WIDGET(s_window), state & ~VK_STATE_NORESIZE);

    content = vk_window_get_child(s_window);
    if(content != NULL)
        vk_widget_resize(content, w, h);

    vk_widget_resize(VK_WIDGET(s_window), w, h);
    vk_widget_move(VK_WIDGET(s_window), 0, 0);

    vk_widget_set_state(VK_WIDGET(s_window), state);

    vk_window_update(s_window);
}
