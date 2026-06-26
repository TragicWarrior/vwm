/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007      Bryan Christ <bryan.christ@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *----------------------------------------------------------------------*/

#include <string.h>
#include <time.h>
#include <langinfo.h>

#include <ncursesw/curses.h>

#include <vdk.h>

#include "vwm.h"
#include "panel.h"
#include "private.h"
#include "sched.h"
#include "strings.h"
#include "list.h"
#include "winman.h"
#include "mainmenu.h"

#define     KEY_PLUS            '+'
#define     KEY_CTRL_DOWN       525

#define     KEY_MINUS           '-'
#define     KEY_CTRL_UP         566

#define     KEY_GREATER_THAN    '>'
#define     KEY_CTRL_RIGHT      560

#define     KEY_LESS_THAN       '<'
#define     KEY_CTRL_LEFT       545

#define     VWM_HOTKEY_DESKTOP  (27 | (100 << 8))

static VWM_PANEL    *panel_data = NULL;

VWM_PANEL*
vwm_panel_get_data(void)
{
    return panel_data;
}

void
vwm_panel_init(vwm_t *vwm)
{
    VWM_PANEL       *vwm_panel;
    int             max_y, max_x;
    int             has_utf8;

    if(panel_data != NULL) return;

    vwm_panel = (VWM_PANEL*)calloc(1, sizeof(VWM_PANEL));
    panel_data = vwm_panel;

    getmaxyx(vk_screen_get_window(vwm->screen), max_y, max_x);
    {
        const char *term = getenv("TERM");
        has_utf8 = (strcmp(nl_langinfo(CODESET), "UTF-8") == 0);
        if(term != NULL && strcmp(term, "linux") == 0)
            has_utf8 = 0;
    }

    vwm_panel->box = vk_box_create(max_x, 1, VK_BOX_HORIZONTAL, 7);
    vk_box_set_homogeneous(vwm_panel->box, false);
    vk_widget_set_colors(VK_WIDGET(vwm_panel->box),
        COLOR_BLACK, COLOR_WHITE);

    if(has_utf8)
        vwm_panel->msg_default = "  \xe2\x98\xb0";
    else
        vwm_panel->msg_default = " [=]";

    vwm_panel->msg_label = vk_label_create(5);
    vk_widget_set_colors(VK_WIDGET(vwm_panel->msg_label),
        COLOR_WHITE, COLOR_BLUE);
    vk_widget_set_attrs(VK_WIDGET(vwm_panel->msg_label), A_BOLD);
    vk_label_set_text(vwm_panel->msg_label, vwm_panel->msg_default);
    vk_label_update(vwm_panel->msg_label);

    vwm_panel->menubar = vk_menubar_create(1);
    vk_widget_set_colors(VK_WIDGET(vwm_panel->menubar),
        COLOR_BLACK, COLOR_WHITE);
    vk_menubar_set_highlight(vwm_panel->menubar,
        COLOR_WHITE, COLOR_BLUE);
    vwm->menubar = vwm_panel->menubar;

    {
        vk_filler_t *spacer = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(spacer), COLOR_BLACK, COLOR_WHITE);
        vk_box_set_widget(vwm_panel->box, 2, VK_WIDGET(spacer));
    }

    vwm_panel->task_label = vk_label_create(4);
    vk_widget_set_colors(VK_WIDGET(vwm_panel->task_label),
        COLOR_WHITE, COLOR_MAGENTA);
    vwm_panel_update_taskcount(vwm_panel);

    vwm_panel->clock_label = vk_label_create(21);
    vk_widget_set_colors(VK_WIDGET(vwm_panel->clock_label),
        COLOR_BLACK, COLOR_CYAN);
    vwm_panel_update_clock(vwm_panel);

    vwm_panel->activity = vk_activity_create();
    if(has_utf8)
    {
        vk_widget_set_colors(VK_WIDGET(vwm_panel->activity),
            COLOR_WHITE, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(vwm_panel->activity), A_BOLD);
        vk_activity_set_style(vwm_panel->activity, VK_ACTIVITY_DOTS);
    }
    else
    {
        vk_widget_set_colors(VK_WIDGET(vwm_panel->activity),
            COLOR_BLACK, COLOR_CYAN);
        vk_activity_set_style(vwm_panel->activity, VK_ACTIVITY_SPINNER);
    }
    vk_activity_set_speed(vwm_panel->activity, 2);
    vk_activity_start(vwm_panel->activity);

    vk_box_set_widget(vwm_panel->box, 0,
        VK_WIDGET(vwm_panel->msg_label));
    vk_box_set_widget(vwm_panel->box, 1,
        VK_WIDGET(vwm_panel->menubar));
    vk_box_set_widget(vwm_panel->box, 3,
        VK_WIDGET(vwm_panel->task_label));
    vk_box_set_widget(vwm_panel->box, 4,
        VK_WIDGET(vwm_panel->clock_label));
    vk_box_set_widget(vwm_panel->box, 5,
        VK_WIDGET(vwm_panel->activity));

    {
        vk_label_t *pad = vk_label_create(1);
        vk_widget_set_colors(VK_WIDGET(pad), COLOR_BLACK, COLOR_CYAN);
        vk_label_set_text(pad, " ");
        vk_label_update(pad);
        vk_box_set_widget(vwm_panel->box, 6, VK_WIDGET(pad));
    }

    vk_screen_attach_widget(vwm->screen, 0, VK_WIDGET(vwm_panel->box));

    vk_box_update(vwm_panel->box);
    vk_widget_draw(VK_WIDGET(vwm_panel->box));

    {
        char version_str[32];
        int  version_len;

        snprintf(version_str, sizeof(version_str), " VWM %s ", VWM_VERSION);
        version_len = strlen(version_str);

        vwm_panel->status_box = vk_box_create(max_x, 1,
            VK_BOX_HORIZONTAL, 5);
        vk_box_set_homogeneous(vwm_panel->status_box, false);
        vk_widget_set_colors(VK_WIDGET(vwm_panel->status_box),
            COLOR_BLACK, COLOR_WHITE);

        {
            vk_label_t *pad = vk_label_create(1);
            vk_widget_set_colors(VK_WIDGET(pad), COLOR_BLACK, COLOR_WHITE);
            vk_label_set_text(pad, " ");
            vk_label_update(pad);
            vk_box_set_widget(vwm_panel->status_box, 0, VK_WIDGET(pad));
        }

        vwm_panel->status_marquee = vk_marquee_create(1);
        vk_widget_set_expand(VK_WIDGET(vwm_panel->status_marquee));
        vk_widget_set_colors(VK_WIDGET(vwm_panel->status_marquee),
            COLOR_BLACK, COLOR_WHITE);
        vk_marquee_set_direction(vwm_panel->status_marquee, VK_SCROLL_LEFT);
        vk_marquee_set_repeat(vwm_panel->status_marquee, true);
        vk_marquee_set_speed(vwm_panel->status_marquee, 3);
        vk_marquee_set_pause(vwm_panel->status_marquee, 50);
        vk_marquee_set_text(vwm_panel->status_marquee,
            "Alt ~ Menu | Alt d Switch Desktop");

        vwm_panel->version_label = vk_label_create(version_len);
        vk_widget_set_colors(VK_WIDGET(vwm_panel->version_label),
            COLOR_BLACK, COLOR_CYAN);
        vk_label_set_text(vwm_panel->version_label, version_str);
        vk_label_update(vwm_panel->version_label);

        /*
            dtach status indicator -- bottom-right, just left of the VWM
            version label.  Circle: bright green = running under the
            bundled dtach launcher, plain red = not.  Bright-white text on
            a dark-gray field.  Static for the process lifetime, so it's a
            box-owned local (no struct field / updater).  Dark gray wants
            a 16-colour terminal; falls back to the base palette.
        */
        {
            int         under_dtach = (getenv("VWM_SOCK") != NULL);
            int         gray  = (COLORS >= 16) ? 8  : COLOR_BLACK;
            int         white = (COLORS >= 16) ? 15 : COLOR_WHITE;
            int         dot   = under_dtach ? COLOR_GREEN : COLOR_RED;
            const char  *txt  = under_dtach
                                    ? "dtach active " : "dtach inactive ";
            vk_label_t  *dtach_dot = vk_label_create(3);
            vk_label_t  *dtach_txt = vk_label_create((int)strlen(txt));

            /* bold (bright) green for active; a plain, true red for
               inactive -- the bright red read wrong. */
            vk_widget_set_colors(VK_WIDGET(dtach_dot), dot, gray);
            vk_widget_set_attrs(VK_WIDGET(dtach_dot),
                under_dtach ? A_BOLD : A_NORMAL);
            vk_label_set_text(dtach_dot, has_utf8 ? " \xe2\x97\x8f " : " o ");
            vk_label_update(dtach_dot);

            vk_widget_set_colors(VK_WIDGET(dtach_txt), white, gray);
            vk_widget_set_attrs(VK_WIDGET(dtach_txt), A_BOLD);
            vk_label_set_text(dtach_txt, txt);
            vk_label_update(dtach_txt);

            vk_box_set_widget(vwm_panel->status_box, 2,
                VK_WIDGET(dtach_dot));
            vk_box_set_widget(vwm_panel->status_box, 3,
                VK_WIDGET(dtach_txt));
        }

        vk_box_set_widget(vwm_panel->status_box, 1,
            VK_WIDGET(vwm_panel->status_marquee));
        vk_box_set_widget(vwm_panel->status_box, 4,
            VK_WIDGET(vwm_panel->version_label));

        vk_widget_move(VK_WIDGET(vwm_panel->status_box), 0, max_y - 1);
        vk_screen_attach_widget(vwm->screen, 0,
            VK_WIDGET(vwm_panel->status_box));

        vk_box_update(vwm_panel->status_box);
        vk_widget_draw(VK_WIDGET(vwm_panel->status_box));
    }

    INIT_LIST_HEAD(&vwm_panel->msg_list);
}

void
vwm_desktop_prompt_show(void)
{
    vwm_t       *vwm;
    VWM_PANEL   *vwm_panel;
    vk_label_t  *prompt;
    int          max_y, max_x;
    char         text[64];
    int          pos;
    int          surface;

    vwm = vwm_get_instance();
    vwm_panel = vwm_panel_get_data();

    if(vwm_panel->desktop_prompt != NULL) return;

    vwm_menubar_close_dropdown();
    vk_menubar_set_focused(vwm->menubar, false);
    vk_menubar_update(vwm->menubar);
    vwm_calendar_close();

    getmaxyx(vk_screen_get_window(vwm->screen), max_y, max_x);
    (void)max_y;

    pos = 0;
    pos += snprintf(text + pos, sizeof(text) - pos, " Switch desktop (");
    for(int i = 0; i < vwm->surface_count; i++)
    {
        if(i > 0)
            pos += snprintf(text + pos, sizeof(text) - pos, ", ");
        pos += snprintf(text + pos, sizeof(text) - pos, "%d", i + 1);
    }
    snprintf(text + pos, sizeof(text) - pos, "): ");

    prompt = vk_label_create(max_x);
    vk_widget_set_colors(VK_WIDGET(prompt), COLOR_WHITE, COLOR_BLUE);
    vk_widget_set_attrs(VK_WIDGET(prompt), A_BOLD);
    vk_label_set_text(prompt, text);
    vk_label_update(prompt);

    surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_detach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->box));
    vk_screen_attach_widget(vwm->screen, surface, VK_WIDGET(prompt));

    vwm_panel->desktop_prompt = prompt;
}

static void
vwm_desktop_prompt_close(void)
{
    vwm_t       *vwm;
    VWM_PANEL   *vwm_panel;
    int          surface;

    vwm = vwm_get_instance();
    vwm_panel = vwm_panel_get_data();

    if(vwm_panel->desktop_prompt == NULL) return;

    surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_detach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->desktop_prompt));
    vk_label_destroy(vwm_panel->desktop_prompt);
    vwm_panel->desktop_prompt = NULL;

    vk_screen_attach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->box));
    vk_box_update(vwm_panel->box);
}

static void
vwm_teleport_prompt_redraw(void)
{
    VWM_PANEL   *vwm_panel;
    char        text[160];

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel->teleport_prompt == NULL) return;

    snprintf(text, sizeof(text), " Teleport to: %s",
        vwm_panel->teleport_text);
    vk_label_set_text(vwm_panel->teleport_prompt, text);
    vk_label_update(vwm_panel->teleport_prompt);
}

void
vwm_teleport_prompt_show(void)
{
    vwm_t       *vwm;
    VWM_PANEL   *vwm_panel;
    vk_label_t  *prompt;
    int          max_y, max_x;
    int          surface;

    vwm = vwm_get_instance();
    vwm_panel = vwm_panel_get_data();

    if(vwm_panel->teleport_prompt != NULL) return;

    vwm_menubar_close_dropdown();
    vk_menubar_set_focused(vwm->menubar, false);
    vk_menubar_update(vwm->menubar);
    vwm_calendar_close();

    getmaxyx(vk_screen_get_window(vwm->screen), max_y, max_x);
    (void)max_y;

    vwm_panel->teleport_text[0] = '\0';
    vwm_panel->teleport_pos = 0;

    prompt = vk_label_create(max_x);
    vk_widget_set_colors(VK_WIDGET(prompt), COLOR_WHITE, COLOR_BLUE);
    vk_widget_set_attrs(VK_WIDGET(prompt), A_BOLD);
    vk_label_set_text(prompt, " Teleport to: ");
    vk_label_update(prompt);

    surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_detach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->box));
    vk_screen_attach_widget(vwm->screen, surface, VK_WIDGET(prompt));

    vwm_panel->teleport_prompt = prompt;
}

static void
vwm_teleport_prompt_close(void)
{
    vwm_t       *vwm;
    VWM_PANEL   *vwm_panel;
    int          surface;

    vwm = vwm_get_instance();
    vwm_panel = vwm_panel_get_data();

    if(vwm_panel->teleport_prompt == NULL) return;

    surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_detach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->teleport_prompt));
    vk_label_destroy(vwm_panel->teleport_prompt);
    vwm_panel->teleport_prompt = NULL;
    vwm_panel->teleport_text[0] = '\0';
    vwm_panel->teleport_pos = 0;

    vk_screen_attach_widget(vwm->screen, surface,
        VK_WIDGET(vwm_panel->box));
    vk_box_update(vwm_panel->box);
}

int
vwm_panel_ON_KEYSTROKE(int32_t keystroke, void *anything)
{
    vwm_t       *vwm;

    (void)anything;

    vwm = vwm_get_instance();

    if(panel_data->desktop_prompt != NULL)
    {
        if(keystroke == 27)
        {
            vwm_desktop_prompt_close();
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        if(keystroke >= '1' && keystroke <= '0' + vwm->surface_count)
        {
            int target = keystroke - '1';

            vwm_desktop_prompt_close();
            vk_screen_set_surface(vwm->screen, target);
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        return KMIO_HANDLED;
    }

    if(panel_data->teleport_prompt != NULL)
    {
        if(keystroke == 27)
        {
            vwm_teleport_prompt_close();
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        if(keystroke == '\n' || keystroke == '\r' || keystroke == KEY_ENTER)
        {
            char path[128];

            if(panel_data->teleport_pos == 0)
            {
                vwm_teleport_prompt_close();
                vk_screen_refresh(vwm->screen);
                return KMIO_HANDLED;
            }

            snprintf(path, sizeof(path), "%s", panel_data->teleport_text);
            vwm_teleport_prompt_close();
            vk_screen_teleport(vwm->screen, path);
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        if(keystroke == KEY_BACKSPACE || keystroke == 127 || keystroke == 8)
        {
            if(panel_data->teleport_pos > 0)
            {
                panel_data->teleport_pos--;
                panel_data->teleport_text[panel_data->teleport_pos] = '\0';
                vwm_teleport_prompt_redraw();
                vk_screen_refresh(vwm->screen);
            }
            return KMIO_HANDLED;
        }

        if(keystroke >= 32 && keystroke < 127
            && panel_data->teleport_pos
                < (int)sizeof(panel_data->teleport_text) - 1)
        {
            panel_data->teleport_text[panel_data->teleport_pos++] =
                (char)keystroke;
            panel_data->teleport_text[panel_data->teleport_pos] = '\0';
            vwm_teleport_prompt_redraw();
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        return KMIO_HANDLED;
    }

    if(keystroke == vwm->hotkey_wm)
    {
        vwm->state ^= VWM_STATE_ACTIVE;

        if(vwm->state & VWM_STATE_ACTIVE)
            vwm_default_VWM_START();
        else
            vwm_default_VWM_STOP();

        return KMIO_HANDLED;
    }

    if(vwm->state & VWM_STATE_ACTIVE)
    {
        vk_widget_t *top = vk_deck_get_top(vwm->deck);

        if(keystroke == vwm->hotkey_close)
        {
            vwm_default_WINDOW_CLOSE(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_cycle)
        {
            vwm_default_WINDOW_CYCLE();
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_move_up)
        {
            vwm_default_WINDOW_MOVE_UP(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_move_down)
        {
            vwm_default_WINDOW_MOVE_DOWN(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_move_left)
        {
            vwm_default_WINDOW_MOVE_LEFT(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_move_right)
        {
            vwm_default_WINDOW_MOVE_RIGHT(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_grow_h
            || keystroke == KEY_CTRL_DOWN)
        {
            vwm_default_WINDOW_INCREASE_HEIGHT(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_shrink_h
            || keystroke == KEY_CTRL_UP)
        {
            vwm_default_WINDOW_DECREASE_HEIGHT(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_grow_w
            || keystroke == KEY_CTRL_RIGHT)
        {
            vwm_default_WINDOW_INCREASE_WIDTH(top);
            return KMIO_HANDLED;
        }
        else if(keystroke == vwm->hotkey_shrink_w
            || keystroke == KEY_CTRL_LEFT)
        {
            vwm_default_WINDOW_DECREASE_WIDTH(top);
            return KMIO_HANDLED;
        }
        else
        {
            return keystroke;
        }
    }

    if(keystroke == vwm->hotkey_menu)
    {
        vwm_menubar_hotkey();
        return KMIO_HANDLED;
    }

    if(keystroke == vwm->hotkey_desktop)
    {
        vwm_desktop_prompt_show();
        vk_screen_refresh(vwm->screen);
        return KMIO_HANDLED;
    }

    return keystroke;
}

void
vwm_panel_ON_TERM_RESIZED(VWM_PANEL *vwm_panel)
{
    vwm_t       *vwm;
    int         max_y, max_x;

    if(vwm_panel == NULL) return;

    if(vwm_panel->desktop_prompt != NULL)
        vwm_desktop_prompt_close();

    if(vwm_panel->teleport_prompt != NULL)
        vwm_teleport_prompt_close();

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), max_y, max_x);

    vk_widget_resize(VK_WIDGET(vwm_panel->box), max_x, 1);
    vk_box_update(vwm_panel->box);

    vk_widget_resize(VK_WIDGET(vwm_panel->status_box), max_x, 1);
    vk_widget_move(VK_WIDGET(vwm_panel->status_box), 0, max_y - 1);
    vk_box_update(vwm_panel->status_box);

    vwm_calendar_close();

    (void)max_y;
}

void
vwm_panel_ON_CLOCK_TICK(VWM_PANEL *vwm_panel)
{
    if(vwm_panel == NULL) return;

    vwm_panel->clock++;

    vwm_panel_update_throbber(vwm_panel);

    if((vwm_panel->clock % 5) == 0)
    {
        vwm_panel_update_taskcount(vwm_panel);
    }

    if((vwm_panel->clock % VWM_CLOCK_TICKS_PER_SEC) == 0)
    {
        vwm_panel_update_clock(vwm_panel);
    }

    vwm_panel_display(vwm_panel);

    vk_box_update(vwm_panel->box);

    vk_marquee_run(vwm_panel->status_marquee);
    vk_box_update(vwm_panel->status_box);
}

void
vwm_panel_update_throbber(VWM_PANEL *panel)
{
    vk_activity_run(panel->activity);
}

void
vwm_panel_update_taskcount(VWM_PANEL *panel)
{
    extern vwm_sched_t  *sched;
    char                buf[8];
    int                 n;

    n = vwm_sched_active_count(sched);
    snprintf(buf, sizeof(buf), " %2d ", n);

    vk_label_set_text(panel->task_label, buf);
    vk_label_update(panel->task_label);
}

void
vwm_panel_update_clock(VWM_PANEL *panel)
{
    time_t          clock;
    struct tm       *local_time;
    char            buf[80];

    clock = time(NULL);
    local_time = localtime((time_t*)&clock);

    snprintf(buf, sizeof(buf), " %02d/%02d/%04d %02d:%02d:%02d ",
        local_time->tm_mon + 1, local_time->tm_mday,
        local_time->tm_year + 1900,
        local_time->tm_hour, local_time->tm_min, local_time->tm_sec);

    vk_label_set_text(panel->clock_label, buf);
    vk_label_update(panel->clock_label);
}

void
vwm_panel_display(VWM_PANEL *vwm_panel)
{
    VWM_PANEL_MSG       *vwm_panel_msg;

    if(list_empty(&vwm_panel->msg_list))
    {
        vk_label_set_text(vwm_panel->msg_label, vwm_panel->msg_default);
        vk_label_update(vwm_panel->msg_label);
        return;
    }

    vwm_panel_msg = list_first_entry(&vwm_panel->msg_list,
        VWM_PANEL_MSG, list);

    vk_label_set_text(vwm_panel->msg_label, vwm_panel_msg->msg);
    vk_label_update(vwm_panel->msg_label);
}

uintmax_t
vwm_panel_message_add(char *msg, int timeout)
{
    VWM_PANEL       *vwm_panel;
    VWM_PANEL_MSG   *vwm_panel_msg;

    if(msg == NULL) return 0;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return 0;

    if(timeout == 0 || timeout > VWM_PANEL_MSG_TTL_MAX)
        timeout = VWM_PANEL_MSG_TTL_MAX;

    vwm_panel_msg = (VWM_PANEL_MSG*)calloc(1, sizeof(VWM_PANEL_MSG));
    vwm_panel_msg->msg = strdup(msg);
    vwm_panel_msg->msg_len = strlen(msg);
    vwm_panel_msg->timeout = timeout;
    vwm_panel_msg->touch_val = timeout;
    vwm_panel_msg->msg_id.msg_addr = vwm_panel_msg;

    list_add(&vwm_panel_msg->list, &vwm_panel->msg_list);
    vwm_panel->msg_count++;

    return vwm_panel_msg->msg_id.msg_handle;
}

void
vwm_panel_message_del(uintmax_t msg_id)
{
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return;

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        list_del(pos);

        free(vwm_panel_msg->msg);
        free(vwm_panel_msg);

        vwm_panel->msg_count--;

    }

    return;
}

int
vwm_panel_message_touch(uintmax_t msg_id)
{
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return -1;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return -1;

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        vwm_panel_msg->timeout = vwm_panel_msg->touch_val;
        return (int)vwm_panel_msg->timeout;
    }

    return -1;
}

int
vwm_panel_message_promote(uintmax_t msg_id)
{
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return -1;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return -1;

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        list_move(pos, &vwm_panel->msg_list);
    }

    return 0;
}

uintmax_t
vwm_panel_message_find(char *msg)
{
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg == NULL) return 0;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return 0;

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(strncmp(vwm_panel_msg->msg, msg, vwm_panel_msg->msg_len) == 0) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg == NULL) return 0;

    return vwm_panel_msg->msg_id.msg_handle;
}

void
vwm_panel_set_status(const char *text)
{
    VWM_PANEL   *vwm_panel;

    vwm_panel = vwm_panel_get_data();
    if(vwm_panel == NULL) return;

    vk_marquee_set_text(vwm_panel->status_marquee, text);
}

static int
vwm_calendar_kmio(vk_object_t *object, int32_t keystroke)
{
    vk_calendar_t   *calendar = VK_CALENDAR(object);

    switch(keystroke)
    {
        case KEY_LEFT:
            vk_calendar_prev_month(calendar);
            break;

        case KEY_RIGHT:
            vk_calendar_next_month(calendar);
            break;

        case 27:
            vwm_calendar_close();
            return 0;

        default:
            return -1;
    }

    vk_calendar_update(calendar);

    {
        vwm_t *vwm = vwm_get_instance();
        if(vwm->calendar_popup != NULL)
            vk_window_update(vwm->calendar_popup);
    }

    return 0;
}

void
vwm_calendar_toggle(void)
{
    vwm_t           *vwm;
    VWM_PANEL       *vwm_panel;

    vwm = vwm_get_instance();
    vwm_panel = vwm_panel_get_data();

    if(vwm->calendar_popup != NULL)
    {
        vwm_calendar_close();
        return;
    }

    {
        vk_calendar_t   *calendar;
        vk_window_t     *window;
        int             clock_x, clock_y;
        int             clock_w, clock_h;
        int             cal_w = 22;
        int             cal_h = 8;
        int             win_w = cal_w + 2;
        int             win_h = cal_h + 2;
        int             pos_x, pos_y;
        int             scr_w, scr_h;

        calendar = vk_calendar_create();
        vk_widget_set_colors(VK_WIDGET(calendar), COLOR_BLUE, COLOR_CYAN);
        vk_calendar_set_highlight(calendar, COLOR_BLACK, COLOR_RED);
        vk_calendar_set_dimmed(calendar, COLOR_BLACK, COLOR_CYAN);
        vk_calendar_set_dimmed_attrs(calendar, A_BOLD);
        vk_calendar_set_header_colors(calendar, COLOR_WHITE, COLOR_CYAN);
        vk_calendar_set_header_attrs(calendar, A_BOLD);
        vk_object_set_kmio(VK_OBJECT(calendar), vwm_calendar_kmio);

        window = vk_window_create(win_w, win_h);
        vk_window_set_border_style(window, VK_BORDER_SINGLE);
        vk_window_set_border_colors(window, COLOR_BLACK, COLOR_CYAN);
        vk_window_set_child(window, VK_WIDGET(calendar));

        vk_widget_get_position(VK_WIDGET(vwm_panel->clock_label),
            &clock_x, &clock_y);
        vk_widget_get_metrics(VK_WIDGET(vwm_panel->clock_label),
            &clock_w, &clock_h);
        getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
        (void)scr_h;

        pos_x = clock_x + clock_w - win_w;
        if(pos_x < 0) pos_x = 0;
        if(pos_x + win_w > scr_w) pos_x = scr_w - win_w;

        pos_y = 1;

        vk_widget_move(VK_WIDGET(window), pos_x, pos_y);
        vk_screen_attach_widget(vwm->screen,
            vk_screen_get_active_surface(vwm->screen), VK_WIDGET(window));

        vk_calendar_update(calendar);
        vk_window_update(window);

        vwm->calendar_popup = window;
    }
}

void
vwm_calendar_close(void)
{
    vwm_t           *vwm;
    vk_window_t     *popup;
    vk_calendar_t   *calendar;

    vwm = vwm_get_instance();
    popup = vwm->calendar_popup;

    if(popup == NULL) return;

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(popup));

    calendar = VK_CALENDAR(vk_window_get_child(popup));

    /* detach the child while both are still valid: otherwise the window
       dtor list_del()s a freed calendar node and corrupts the heap */
    vk_window_set_child(popup, NULL);

    vk_calendar_destroy(calendar);
    vk_window_destroy(popup);

    vwm->calendar_popup = NULL;
}
