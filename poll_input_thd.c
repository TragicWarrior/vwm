#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "poll_input_thd.h"
#include "private.h"
#include "mainmenu.h"
#include "manage_apps.h"
#include "manage_hotkeys.h"
#include "panel.h"
#include "winman.h"
#include "events.h"

enum
{
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE,
};

enum
{
    ZONE_SCREEN = 0,
    ZONE_PANEL,
    ZONE_MENU,
    ZONE_CALENDAR,
    ZONE_MANAGE_APPS,
    ZONE_MANAGE_HOTKEYS,
    ZONE_STATUS_BAR,
    ZONE_CLOSE_BTN,
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
}

static int
classify_mouse(vwm_t *vwm, int mx, int my, vk_widget_t **hit_out)
{
    vk_widget_t *hit;
    int         wx, wy, ww, wh;
    int         rx, ry;
    uint32_t    state;

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
        vk_widget_t *dd_pop = vwm_manage_apps_get_dropdown_popup();
        if(dd_pop != NULL)
        {
            vk_widget_get_position(dd_pop, &wx, &wy);
            vk_widget_get_metrics(dd_pop, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = dd_pop;
                return ZONE_MANAGE_APPS;
            }
        }

        vk_widget_t *apps_warning =
            vwm_manage_apps_get_warning_popup();
        if(apps_warning != NULL)
        {
            vk_widget_get_position(apps_warning, &wx, &wy);
            vk_widget_get_metrics(apps_warning, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = apps_warning;
                return ZONE_MANAGE_APPS;
            }
        }

        vk_widget_t *load_pop = vwm_manage_apps_get_load_popup();
        if(load_pop != NULL)
        {
            vk_widget_get_position(load_pop, &wx, &wy);
            vk_widget_get_metrics(load_pop, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = load_pop;
                return ZONE_MANAGE_APPS;
            }
        }

        vk_widget_t *edit_pop = vwm_manage_apps_get_edit_popup();
        if(edit_pop != NULL)
        {
            vk_widget_get_position(edit_pop, &wx, &wy);
            vk_widget_get_metrics(edit_pop, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = edit_pop;
                return ZONE_MANAGE_APPS;
            }
        }

        vk_widget_get_position(VK_WIDGET(vwm->manage_apps_popup),
            &wx, &wy);
        vk_widget_get_metrics(VK_WIDGET(vwm->manage_apps_popup),
            &ww, &wh);

        if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
        {
            *hit_out = VK_WIDGET(vwm->manage_apps_popup);
            return ZONE_MANAGE_APPS;
        }
    }

    if(vwm->manage_hotkeys_popup != NULL)
    {
        vk_widget_t *hk_confirm =
            vwm_manage_hotkeys_get_confirm_popup();
        if(hk_confirm != NULL)
        {
            vk_widget_get_position(hk_confirm, &wx, &wy);
            vk_widget_get_metrics(hk_confirm, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = hk_confirm;
                return ZONE_MANAGE_HOTKEYS;
            }
        }

        vk_widget_t *hk_error =
            vwm_manage_hotkeys_get_error_popup();
        if(hk_error != NULL)
        {
            vk_widget_get_position(hk_error, &wx, &wy);
            vk_widget_get_metrics(hk_error, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = hk_error;
                return ZONE_MANAGE_HOTKEYS;
            }
        }

        vk_widget_t *hk_warning =
            vwm_manage_hotkeys_get_warning_popup();
        if(hk_warning != NULL)
        {
            vk_widget_get_position(hk_warning, &wx, &wy);
            vk_widget_get_metrics(hk_warning, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = hk_warning;
                return ZONE_MANAGE_HOTKEYS;
            }
        }

        vk_widget_t *hk_load = vwm_manage_hotkeys_get_load_popup();
        if(hk_load != NULL)
        {
            vk_widget_get_position(hk_load, &wx, &wy);
            vk_widget_get_metrics(hk_load, &ww, &wh);

            if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
            {
                *hit_out = hk_load;
                return ZONE_MANAGE_HOTKEYS;
            }
        }

        vk_widget_get_position(VK_WIDGET(vwm->manage_hotkeys_popup),
            &wx, &wy);
        vk_widget_get_metrics(VK_WIDGET(vwm->manage_hotkeys_popup),
            &ww, &wh);

        if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
        {
            *hit_out = VK_WIDGET(vwm->manage_hotkeys_popup);
            return ZONE_MANAGE_HOTKEYS;
        }
    }

    if(vwm->calendar_popup != NULL)
    {
        vk_widget_get_position(VK_WIDGET(vwm->calendar_popup), &wx, &wy);
        vk_widget_get_metrics(VK_WIDGET(vwm->calendar_popup), &ww, &wh);

        if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
        {
            *hit_out = VK_WIDGET(vwm->calendar_popup);
            return ZONE_CALENDAR;
        }
    }

    if(vwm->menu != NULL)
    {
        vk_widget_get_position(VK_WIDGET(vwm->menu), &wx, &wy);
        vk_widget_get_metrics(VK_WIDGET(vwm->menu), &ww, &wh);

        if(mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
        {
            *hit_out = VK_WIDGET(vwm->menu);
            return ZONE_MENU;
        }
    }

    hit = vk_deck_hit_test(vwm->deck, mx, my);
    if(hit == NULL) return ZONE_SCREEN;

    *hit_out = hit;

    vk_widget_get_position(hit, &wx, &wy);
    vk_widget_get_metrics(hit, &ww, &wh);
    rx = mx - wx;
    ry = my - wy;

    if(ry == 0 && rx >= ww - (int)sizeof("[X]") + 1 && rx < ww)
        return ZONE_CLOSE_BTN;

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
    vk_widget_t *old_top;

    if(widget == vk_deck_get_top(vwm->deck)) return;

    old_top = vk_deck_get_top(vwm->deck);
    vk_deck_set_top(vwm->deck, widget);
    if(old_top != NULL) vk_window_update(VK_WINDOW(old_top));
    vk_window_update(VK_WINDOW(widget));
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

        if(keystroke == KEY_RESIZE)
        {
            vk_screen_resize(vwm->screen);
            vwm_panel_ON_TERM_RESIZED(vwm_panel_get_data());
            vwm_dropdown_ON_TERM_RESIZED();

            if(vwm_manage_hotkeys_is_open())
                vwm_manage_hotkeys_handle_resize();

            if(vwm_manage_apps_is_open())
                vwm_manage_apps_handle_resize();

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
                if(bs & (BUTTON1_RELEASED | BUTTON1_CLICKED))
                {
                    apply_drag_position(mouse_event);
                    drag_mode = DRAG_NONE;
                    drag_widget = NULL;
                }
                else
                {
                    apply_drag_position(mouse_event);
                }

                vk_screen_refresh(vwm->screen);
                ctx_poll_input->did_work = 1;
                pt_yield(ctx_poll_input);
                continue;
            }

            zone = classify_mouse(vwm, mouse_event->x,
                mouse_event->y, &hit);

            if(vwm->manage_hotkeys_popup != NULL &&
               zone != ZONE_MANAGE_HOTKEYS &&
               (bs & BUTTON1_PRESSED))
            {
                vwm_manage_hotkeys_close();
                vk_screen_refresh(vwm->screen);
            }

            if(vwm->manage_apps_popup != NULL &&
               zone != ZONE_MANAGE_APPS &&
               (bs & BUTTON1_PRESSED))
            {
                vwm_manage_apps_close();
                vk_screen_refresh(vwm->screen);
            }

            if(vwm->menu != NULL && zone != ZONE_MENU &&
               zone != ZONE_PANEL && (bs & BUTTON1_PRESSED))
            {
                vwm_menubar_close_dropdown();
                vk_menubar_set_focused(vwm->menubar, false);
                vk_menubar_update(vwm->menubar);
                vk_screen_refresh(vwm->screen);
            }

            if(vwm->calendar_popup != NULL && zone != ZONE_CALENDAR &&
               zone != ZONE_STATUS_BAR && zone != ZONE_PANEL &&
               (bs & BUTTON1_PRESSED))
            {
                vwm_calendar_close();
                vk_screen_refresh(vwm->screen);
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

                            vk_widget_get_position(
                                VK_WIDGET(p->clock_label),
                                &ck_x, &ck_y);
                            vk_widget_get_metrics(
                                VK_WIDGET(p->clock_label),
                                &ck_w, &ck_h);

                            if(mouse_event->x >= ck_x &&
                               mouse_event->x < ck_x + ck_w)
                            {
                                if(bs & BUTTON1_PRESSED)
                                    menubar_ate_press = true;

                                vwm_calendar_toggle();
                            }
                            else
                            {
                                if(bs & BUTTON1_PRESSED)
                                    menubar_ate_press = true;

                                vwm_calendar_close();
                                vwm_menubar_hotkey();
                            }
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
                vk_screen_refresh(vwm->screen);
            }
        }

        ctx_poll_input->did_work = 1;
        pt_yield(ctx_poll_input);
    }
    while(!(*ctx_poll_input->shutdown));

    return PT_DONE;
}
