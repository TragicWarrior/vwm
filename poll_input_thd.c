#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "poll_input_thd.h"
#include "private.h"
#include "mainmenu.h"
#include "manage_apps.h"
#include "manage_hotkeys.h"
#include "manage_settings.h"
#include "manage_windows.h"
#include "bkgd.h"
#include "modules.h"
#include "panel.h"
#include "winman.h"
#include "events.h"
#include "screensaver.h"

enum
{
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE,
    DRAG_SCROLL,
};

enum
{
    ZONE_SCREEN = 0,
    ZONE_PANEL,
    ZONE_MENU,
    ZONE_CALENDAR,
    ZONE_MANAGE_APPS,
    ZONE_MANAGE_HOTKEYS,
    ZONE_MANAGE_SETTINGS,
    ZONE_STATUS_BAR,
    ZONE_CLOSE_BTN,
    ZONE_MINIMIZE_BTN,
    ZONE_RESIZE_CORNER,
    ZONE_FRAME,
    ZONE_CONTENT,
};

static int      drag_mode = DRAG_NONE;
static int      drag_anchor_x;
static int      drag_anchor_y;
static int      drag_orig_wx;
static int      drag_orig_wy;
static int      drag_orig_ww;
static int      drag_orig_wh;
static vk_widget_t *drag_widget = NULL;
static bool     menubar_ate_press = false;

/* module-supplied handler for a DRAG_SCROLL (scrollbar-thumb) drag: the poll
   loop owns the capture and release, and calls this with the live cursor
   position so the widget can update its scroll offset.  set via
   vwm_set_scroll_drag_cb(); NULL until a scroll-capable module registers. */
static void (*scroll_drag_cb)(vk_widget_t *widget, int mx, int my) = NULL;

/*
    Cancel any active drag if its target widget matches.  Called from
    vwmterm_ON_CLOSE before vterm_destroy: without this, a queued mouse
    event would drive vk_widget_resize on a window whose vterm was
    just freed, and the dangling vterm pointer captured by the
    ON_RESIZE handler would fault inside vterm_buffer_realloc.
*/
void
vwm_cancel_drag_for_widget(vk_widget_t *widget)
{
    if(drag_widget == widget)
    {
        drag_widget = NULL;
        drag_mode = DRAG_NONE;
    }
}

/* register the handler invoked while a scrollbar-thumb drag is active */
void
vwm_set_scroll_drag_cb(void (*cb)(vk_widget_t *widget, int mx, int my))
{
    scroll_drag_cb = cb;
}

/* begin a captured scrollbar-thumb drag on `widget`.  the poll loop then
   routes every mouse event to scroll_drag_cb until the button is released, so
   an off-window or coalesced release can't leave the thumb stuck to the
   cursor. */
void
vwm_begin_scroll_drag(vk_widget_t *widget)
{
    drag_mode = DRAG_SCROLL;
    drag_widget = widget;
}

static void
apply_drag_position(MEVENT *mouse_event)
{
    if(drag_mode == DRAG_MOVE)
    {
        int dx = mouse_event->x - drag_anchor_x;
        int dy = mouse_event->y - drag_anchor_y;

        vk_widget_move(drag_widget,
            drag_orig_wx + dx, drag_orig_wy + dy);
    }
    else if(drag_mode == DRAG_RESIZE)
    {
        int new_w = drag_orig_ww + (mouse_event->x - drag_anchor_x);
        int new_h = drag_orig_wh + (mouse_event->y - drag_anchor_y);

        if(new_w < 3) new_w = 3;
        if(new_h < 3) new_h = 3;

        vk_widget_resize(drag_widget, new_w, new_h);
    }
    else if(drag_mode == DRAG_SCROLL)
    {
        if(scroll_drag_cb != NULL)
            scroll_drag_cb(drag_widget, mouse_event->x, mouse_event->y);
    }
}

/* uniform popup bounds-test: when (mx,my) lands inside w, set *hit_out
   and return `zone`; otherwise -1.  w == NULL (popup not open) is a miss,
   which folds the per-popup NULL-check into the same call. */
static int
_mouse_hit(vk_widget_t *w, int mx, int my, vk_widget_t **hit_out, int zone)
{
    int wx, wy, ww, wh;

    if(w == NULL) return -1;

    vk_widget_get_position(w, &wx, &wy);
    vk_widget_get_metrics(w, &ww, &wh);

    if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
    {
        *hit_out = w;
        return zone;
    }

    return -1;
}

static int
classify_mouse(vwm_t *vwm, int mx, int my, vk_widget_t **hit_out)
{
    vk_widget_t *hit;
    int         wx, wy, ww, wh;
    int         rx, ry;
    uint32_t    state;
    int         z;

/* test one candidate; return its zone immediately on a hit */
#define MHIT(w, zone) \
    do { if((z = _mouse_hit((w), mx, my, hit_out, (zone))) >= 0) return z; } \
    while(0)

    *hit_out = NULL;

    if(my == 0) return ZONE_PANEL;

    {
        int scr_h, scr_w;
        getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
        (void)scr_w;
        if(my == scr_h - 1) return ZONE_STATUS_BAR;
    }

    if(vwm->manage_apps_popup != NULL)
    {
        MHIT(vwm_manage_apps_get_dropdown_popup(), ZONE_MANAGE_APPS);
        MHIT(vwm_manage_apps_get_confirm_popup(),  ZONE_MANAGE_APPS);
        MHIT(vwm_manage_apps_get_saved_popup(),    ZONE_MANAGE_APPS);
        MHIT(vwm_manage_apps_get_warning_popup(),  ZONE_MANAGE_APPS);
        MHIT(vwm_manage_apps_get_load_popup(),     ZONE_MANAGE_APPS);
        MHIT(vwm_manage_apps_get_edit_popup(),     ZONE_MANAGE_APPS);
        MHIT(VK_WIDGET(vwm->manage_apps_popup),    ZONE_MANAGE_APPS);
    }

    if(vwm->manage_hotkeys_popup != NULL)
    {
        MHIT(vwm_manage_hotkeys_get_confirm_popup(), ZONE_MANAGE_HOTKEYS);
        MHIT(vwm_manage_hotkeys_get_error_popup(),   ZONE_MANAGE_HOTKEYS);
        MHIT(vwm_manage_hotkeys_get_saved_popup(),   ZONE_MANAGE_HOTKEYS);
        MHIT(vwm_manage_hotkeys_get_warning_popup(), ZONE_MANAGE_HOTKEYS);
        MHIT(vwm_manage_hotkeys_get_load_popup(),    ZONE_MANAGE_HOTKEYS);
        MHIT(VK_WIDGET(vwm->manage_hotkeys_popup),   ZONE_MANAGE_HOTKEYS);
    }

    if(vwm->manage_settings_popup != NULL)
    {
        MHIT(vwm_manage_settings_get_confirm_popup(),      ZONE_MANAGE_SETTINGS);
        MHIT(vwm_manage_settings_get_save_confirm_popup(), ZONE_MANAGE_SETTINGS);
        MHIT(vwm_manage_settings_get_saved_popup(),        ZONE_MANAGE_SETTINGS);
        MHIT(vwm_manage_settings_get_warning_popup(),      ZONE_MANAGE_SETTINGS);
        MHIT(vwm_manage_settings_get_modify_popup(),       ZONE_MANAGE_SETTINGS);
        MHIT(vwm_manage_settings_get_load_popup(),         ZONE_MANAGE_SETTINGS);
        MHIT(VK_WIDGET(vwm->manage_settings_popup),        ZONE_MANAGE_SETTINGS);
    }

    MHIT(VK_WIDGET(vwm->calendar_popup), ZONE_CALENDAR);
    MHIT(VK_WIDGET(vwm->menu),           ZONE_MENU);

#undef MHIT

    hit = vk_deck_hit_test(vwm->deck, mx, my);
    if(hit == NULL) return ZONE_SCREEN;

    *hit_out = hit;

    vk_widget_get_position(hit, &wx, &wy);
    vk_widget_get_metrics(hit, &ww, &wh);
    rx = mx - wx;
    ry = my - wy;

    /* top-border controls, right-aligned with a two-column corner margin:
       [v][X]__ -- [X] spans ww-5..ww-3, [v] spans ww-8..ww-6, each 3 wide
       (must track the drawing in vwm_window_decorate, private.c) */
    if(ry == 0 && rx >= ww - 5 && rx <= ww - 3)
        return ZONE_CLOSE_BTN;

    if(ry == 0 && rx >= ww - 8 && rx <= ww - 6)
        return ZONE_MINIMIZE_BTN;

    state = vk_widget_get_state(hit);
    if(ry == wh - 1 && rx == ww - 1 && !(state & VK_STATE_NORESIZE))
        return ZONE_RESIZE_CORNER;

    if(ry == 0 || ry == wh - 1 || rx == 0 || rx == ww - 1)
        return ZONE_FRAME;

    return ZONE_CONTENT;
}

static void
raise_to_top(vwm_t *vwm, vk_widget_t *widget)
{
    if(widget == vk_deck_get_top(vwm->deck)) return;

    /* set_top fires ON_FINALIZE, which repaints the outgoing and incoming
       top windows -- no manual re-decoration needed */
    vk_deck_set_top(vwm->deck, widget);
}

/*
    Fit every window back into the usable area after a shrink -- e.g. a dtach
    reattach or teleport onto a smaller terminal, which leaves windows at their
    old coordinates and possibly larger than the new screen.  Each window's
    top-left is pulled on-screen and, if it is still larger than the usable
    area, shrunk just enough to fit (see vwm_fit_window_onscreen).  Runs across
    every desktop so off-screen windows on inactive decks are fixed too; a
    no-op for windows that already fit.
*/
static void
clamp_windows_onscreen(vwm_t *vwm)
{
    int scr_h, scr_w;
    int s;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    for(s = 0; s < vwm->surface_count; s++)
    {
        vk_deck_t   *deck = vwm->decks[s];
        int          n, i;

        if(deck == NULL) continue;
        n = vk_deck_count(deck);

        for(i = 0; i < n; i++)
        {
            vk_widget_t *w = vk_deck_get_widget(deck, i);

            if(w == NULL) continue;

            vwm_fit_window_onscreen(w, scr_w, scr_h);
        }
    }
}

static void
begin_drag(int mode, vk_widget_t *widget, MEVENT *mouse_event)
{
    int wx, wy, ww, wh;

    drag_mode = mode;
    drag_widget = widget;
    drag_anchor_x = mouse_event->x;
    drag_anchor_y = mouse_event->y;
    vk_widget_get_position(widget, &wx, &wy);
    vk_widget_get_metrics(widget, &ww, &wh);
    drag_orig_wx = wx;
    drag_orig_wy = wy;
    drag_orig_ww = ww;
    drag_orig_wh = wh;
}

pt_t
vwm_poll_input(void * const env)
{
    int32_t             keystroke;
    MEVENT              *mouse_event;
    vwm_t               *vwm;
    int                 retval;

    vwm_sched_ctx_t     *ctx_poll_input;

    ctx_poll_input = (vwm_sched_ctx_t *)env;
    mouse_event = (MEVENT*)ctx_poll_input->anything;
    vwm = vwm_get_instance();

	pt_resume(ctx_poll_input);

    do
    {
        keystroke = vk_kmio_fetch(mouse_event);

        if(keystroke == -1)
        {
            pt_yield(ctx_poll_input);
            continue;
        }

        /* while the screensaver is up, all input is locked to it -- EXCEPT a
           terminal resize (e.g. a dtach reattach onto a different-size tty),
           which we let through so the fullscreen saver overlay and the locked
           program's vterm follow the new geometry.  Only the overlay is
           resized; the desktop beneath stays hidden (no clamp/repaint of the
           deck), so the lock is never broken by a resize. */
        if(vwm_screensaver_is_active())
        {
            if(keystroke == KEY_RESIZE)
            {
                vk_screen_resize(vwm->screen);
                vwm_screensaver_resize();
            }
            else
                vwm_screensaver_input(keystroke, mouse_event);

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        vwm_screensaver_note_activity();

        if(keystroke == KEY_RESIZE)
        {
            vk_screen_resize(vwm->screen);

            /* a dtach / abduco reattach also arrives as KEY_RESIZE (the
               client sends SIGWINCH via -r winch).  Re-arm input against
               the current tty so the mouse survives a detach/reattach
               cycle -- in particular when the user ran `reset` on the
               detached terminal, which clears the modes vwm set at
               startup.  Same re-arm the teleport handler performs. */
            vwm_input_rearm(vwm);

            /* drop every cached wallpaper -- the surface canvases were
               just wresized and the cache callback's geometry mismatch
               check has been observed to miss the change in practice
               (apparently a quirk of getmaxyx returning stale values
               on a freshly-wresized window in some ncurses versions).
               KEY_RESIZE is the authoritative signal that geometry
               changed; invalidate here so the next refresh rebuilds
               every cache at the new size. */
            vwm_invalidate_wallpaper_cache_all();

            vwm_panel_ON_TERM_RESIZED(vwm_panel_get_data());
            vwm_dropdown_ON_TERM_RESIZED();

            if(vwm_manage_hotkeys_is_open())
                vwm_manage_hotkeys_handle_resize();

            if(vwm_manage_apps_is_open())
                vwm_manage_apps_handle_resize();

            if(vwm_manage_settings_is_open())
                vwm_manage_settings_handle_resize();

            if(vwm_manage_windows_is_open())
                vwm_manage_windows_handle_resize();

            /* the terminal may have shrunk (e.g. dtach reattach onto a
               smaller tty) -- pull any now-off-screen window back so its
               frame stays grabbable */
            clamp_windows_onscreen(vwm);

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        /* a surface-attached system tool (e.g. the print dialog) grabs all
           input while open; its kmio handles KEY_MOUSE itself */
        if(vwm->tool_window != NULL)
        {
            vk_object_push_keystroke(VK_OBJECT(vwm->tool_window), keystroke);
            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        if(keystroke == KEY_MOUSE)
        {
            vk_widget_t     *hit = NULL;
            int             zone;
            mmask_t         bs = mouse_event->bstate;

            vwm->cursor_x = mouse_event->x;
            vwm->cursor_y = mouse_event->y;

            if(drag_mode != DRAG_NONE && drag_widget != NULL)
            {
                /* coalesce queued drag-move events down to the latest
                   position: the window jumps to the current cursor in one
                   move + one refresh instead of replaying a backlog of
                   stale positions.  stop when a button release is reached
                   (which ends the drag) or the queue is empty. */
                while(!(mouse_event->bstate &
                        (BUTTON1_RELEASED | BUTTON1_CLICKED)))
                {
                    MEVENT next;

                    if(vk_kmio_mouse_drain(&next) != 0) break;
                    *mouse_event = next;
                }

                vwm->cursor_x = mouse_event->x;
                vwm->cursor_y = mouse_event->y;

                apply_drag_position(mouse_event);

                if(mouse_event->bstate & (BUTTON1_RELEASED | BUTTON1_CLICKED))
                {
                    drag_mode = DRAG_NONE;
                    drag_widget = NULL;
                }

                vk_screen_refresh(vwm->screen);
                ctx_poll_input->did_work = 1;
                pt_yield(ctx_poll_input);
                continue;
            }

            zone = classify_mouse(vwm, mouse_event->x,
                mouse_event->y, &hit);

            /* Manage Apps / Hotkeys / Settings intentionally do NOT close
               on an outside click -- dismiss them with their Close/Cancel
               button or Esc.  (The menubar dropdown and calendar popup
               below still close on an outside press, which is the
               conventional behavior for a menu / transient popup.) */

            if(vwm->menu != NULL && zone != ZONE_MENU &&
               zone != ZONE_PANEL && (bs & BUTTON1_PRESSED))
            {
                vwm_menubar_close_dropdown();
                vk_menubar_set_focused(vwm->menubar, false);
                vk_menubar_update(vwm->menubar);
            }

            if(vwm->calendar_popup != NULL && zone != ZONE_CALENDAR &&
               zone != ZONE_STATUS_BAR && zone != ZONE_PANEL &&
               (bs & BUTTON1_PRESSED))
            {
                vwm_calendar_close();
            }

            switch(zone)
            {
                case ZONE_PANEL:
                {
                    if(bs & BUTTON1_CLICKED && menubar_ate_press)
                    {
                        menubar_ate_press = false;
                        break;
                    }

                    if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                    {
                        int mb_x, mb_y, mb_w, mb_h;
                        vk_widget_get_position(
                            VK_WIDGET(vwm->menubar), &mb_x, &mb_y);
                        vk_widget_get_metrics(
                            VK_WIDGET(vwm->menubar), &mb_w, &mb_h);

                        if(mouse_event->x >= mb_x &&
                           mouse_event->x < mb_x + mb_w)
                        {
                            int hit_idx = vk_menubar_hit_test(
                                vwm->menubar,
                                mouse_event->x - mb_x);

                            if(hit_idx >= 0)
                            {
                                if(bs & BUTTON1_PRESSED)
                                    menubar_ate_press = true;

                                vwm_calendar_close();

                                vk_menubar_set_curr(vwm->menubar, hit_idx);
                                vk_menubar_set_focused(vwm->menubar, true);
                                vk_menubar_update(vwm->menubar);

                                if(vwm->menu != NULL &&
                                   vwm->menu_item_idx == hit_idx)
                                {
                                    vwm_menubar_close_dropdown();
                                }
                                else
                                {
                                    if(vwm->menu != NULL)
                                        vwm_menubar_close_dropdown();
                                    vk_menubar_exec_curr(vwm->menubar);
                                }
                            }
                        }
                        else
                        {
                            VWM_PANEL *p = vwm_panel_get_data();
                            int ck_x, ck_y, ck_w, ck_h;
                            int tk_x, tk_y, tk_w, tk_h;

                            vk_widget_get_position(
                                VK_WIDGET(p->clock_label),
                                &ck_x, &ck_y);
                            vk_widget_get_metrics(
                                VK_WIDGET(p->clock_label),
                                &ck_w, &ck_h);

                            vk_widget_get_position(
                                VK_WIDGET(p->task_label),
                                &tk_x, &tk_y);
                            vk_widget_get_metrics(
                                VK_WIDGET(p->task_label),
                                &tk_w, &tk_h);

                            if(mouse_event->x >= ck_x &&
                               mouse_event->x < ck_x + ck_w)
                            {
                                if(bs & BUTTON1_PRESSED)
                                    menubar_ate_press = true;

                                if(strcmp(vwm->date_click_action,
                                    "calendar") == 0)
                                {
                                    vwm_calendar_toggle();
                                }
                                else
                                {
                                    vwm_module_t *mod =
                                        vwm_module_find_by_title(
                                            vwm->date_click_action);
                                    if(mod != NULL)
                                        vwm_menu_helper(NULL, mod);
                                }
                            }
                            else if(mouse_event->x >= tk_x &&
                                    mouse_event->x < tk_x + tk_w)
                            {
                                if(bs & BUTTON1_PRESSED)
                                    menubar_ate_press = true;

                                if(strcmp(vwm->task_indicator_action,
                                    "none") == 0)
                                {
                                    vwm_calendar_close();
                                    vwm_menubar_hotkey();
                                }
                                else
                                {
                                    vwm_module_t *mod =
                                        vwm_module_find_by_title(
                                            vwm->task_indicator_action);
                                    if(mod != NULL)
                                        vwm_menu_helper(NULL, mod);
                                }
                            }
                            /* clicks elsewhere on the panel do nothing */
                        }
                    }

                    break;
                }

                case ZONE_MENU:
                {
                    vwm_dropdown_mouse(mouse_event);
                    break;
                }

                case ZONE_CLOSE_BTN:
                {
                    if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                    {
                        raise_to_top(vwm, hit);
                        vwm_default_WINDOW_CLOSE(hit);
                    }

                    break;
                }

                case ZONE_MINIMIZE_BTN:
                {
                    if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                        vwm_minimize_window(hit);

                    break;
                }

                case ZONE_RESIZE_CORNER:
                {
                    if(bs & BUTTON1_PRESSED)
                    {
                        raise_to_top(vwm, hit);
                        begin_drag(DRAG_RESIZE, hit, mouse_event);
                    }
                    else if(bs & BUTTON1_CLICKED)
                    {
                        raise_to_top(vwm, hit);
                    }

                    break;
                }

                case ZONE_FRAME:
                {
                    if(bs & BUTTON1_PRESSED)
                    {
                        raise_to_top(vwm, hit);
                        begin_drag(DRAG_MOVE, hit, mouse_event);
                    }
                    else if(bs & BUTTON1_CLICKED)
                    {
                        raise_to_top(vwm, hit);
                    }

                    break;
                }

                case ZONE_CONTENT:
                {
                    if(bs & (BUTTON1_PRESSED | BUTTON1_CLICKED))
                        raise_to_top(vwm, hit);

                    vk_object_push_keystroke(VK_OBJECT(hit), keystroke);
                    break;
                }

                case ZONE_CALENDAR:
                {
                    vk_calendar_t *cal = VK_CALENDAR(
                        vk_window_get_child(vwm->calendar_popup));
                    int cal_x, cal_y, cal_w, cal_h;
                    int rx, ry;

                    vk_widget_get_position(VK_WIDGET(vwm->calendar_popup),
                        &cal_x, &cal_y);
                    vk_widget_get_metrics(VK_WIDGET(vwm->calendar_popup),
                        &cal_w, &cal_h);
                    rx = mouse_event->x - cal_x;
                    ry = mouse_event->y - cal_y;

                    if(bs & BUTTON4_PRESSED)
                    {
                        vk_calendar_prev_month(cal);
                        vk_calendar_update(cal);
                        vk_window_update(vwm->calendar_popup);
                    }
                    else if(bs & BUTTON5_PRESSED)
                    {
                        vk_calendar_next_month(cal);
                        vk_calendar_update(cal);
                        vk_window_update(vwm->calendar_popup);
                    }
                    else if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                    {
                        if(ry == 1 && rx == 1)
                        {
                            vk_calendar_prev_month(cal);
                            vk_calendar_update(cal);
                            vk_window_update(vwm->calendar_popup);
                        }
                        else if(ry == 1 && rx == cal_w - 2)
                        {
                            vk_calendar_next_month(cal);
                            vk_calendar_update(cal);
                            vk_window_update(vwm->calendar_popup);
                        }
                    }

                    break;
                }

                case ZONE_MANAGE_APPS:
                {
                    vwm_manage_apps_mouse(mouse_event);
                    break;
                }

                case ZONE_MANAGE_HOTKEYS:
                {
                    vwm_manage_hotkeys_mouse(mouse_event);
                    break;
                }

                case ZONE_MANAGE_SETTINGS:
                {
                    vwm_manage_settings_mouse(mouse_event);
                    break;
                }

                case ZONE_STATUS_BAR:
                    break;

                case ZONE_SCREEN:
                    break;
            }

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        if(vwm->manage_settings_popup != NULL)
        {
            vk_object_push_keystroke(
                VK_OBJECT(vwm->manage_settings_popup), keystroke);

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        if(vwm->manage_hotkeys_popup != NULL)
        {
            vk_object_push_keystroke(
                VK_OBJECT(vwm->manage_hotkeys_popup), keystroke);

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        if(vwm->manage_apps_popup != NULL)
        {
            vk_object_push_keystroke(
                VK_OBJECT(vwm->manage_apps_popup), keystroke);

            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        if(vwm->calendar_popup != NULL)
        {
            retval = vk_object_push_keystroke(
                VK_OBJECT(vk_window_get_child(vwm->calendar_popup)),
                keystroke);

            if(retval == 0)
            {
                vk_screen_refresh(vwm->screen);
                ctx_poll_input->did_work = 1;
                pt_yield(ctx_poll_input);
                continue;
            }

            if(keystroke == 27 || keystroke == vwm->hotkey_menu)
            {
                vwm_calendar_close();
                vk_screen_refresh(vwm->screen);
                ctx_poll_input->did_work = 1;
                pt_yield(ctx_poll_input);
                continue;
            }
        }

        retval = vwm_menubar_ON_KEYSTROKE(keystroke);
        if(retval == KMIO_HANDLED)
        {
            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        retval = vwm_panel_ON_KEYSTROKE(keystroke, NULL);
        if(retval == KMIO_HANDLED)
        {
            vk_screen_refresh(vwm->screen);
            ctx_poll_input->did_work = 1;
            pt_yield(ctx_poll_input);
            continue;
        }

        {
            vk_widget_t *top = vk_deck_get_top(vwm->deck);

            if(top != NULL)
            {
                vk_object_push_keystroke(VK_OBJECT(top), keystroke);
                /*
                    fall-through keystrokes (no menubar / panel / dialog
                    handled them; no popup is open) are pushed to the
                    deck-top widget -- in practice almost always a
                    vwmterm.  The keystroke goes through libvterm into
                    the PTY; the visible change is the child's echo,
                    which pt_thread picks up a millisecond later and
                    refreshes from its side.  Refreshing here too would
                    composite the full surface for a keystroke that
                    hasn't produced any visible change yet -- pure
                    double work during shell typing, the dominant
                    interactive case.  Skip it; pt_thread covers the
                    actual visible update.
                */
            }
        }

        ctx_poll_input->did_work = 1;
        pt_yield(ctx_poll_input);
    }
    while(!(*ctx_poll_input->shutdown));

    return PT_DONE;
}
