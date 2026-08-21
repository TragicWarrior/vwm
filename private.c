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
#include <inttypes.h>

#include <vdk.h>

#include "profile.h"
#include "vwm.h"
#include "modules.h"
#include "mainmenu.h"
#include "private.h"
#include "winman.h"
#include "bkgd.h"
#include "panel.h"

void
vwm_window_decorate(vk_window_t *window, WINDOW *canvas, void *anything)
{
    vwm_t       *vwm;
    vk_widget_t *top;
    bool        focused;
    char        buf[32];
    int         len;
    int         y, x;
    uint32_t    state;
    short       pair;
    attr_t      extra;
    cchar_t     cc;
    wchar_t     wch[CCHARW_MAX];
    attr_t      attrs;
    short       dummy;
    int         row, col;
    int         close_col, min_col;
    int         reserved;

    /* interior columns that are chrome, not terminal (a vterm's scrollbar);
       discounted from the size label so it reports the real terminal size */
    reserved = (anything != NULL) ? *(const int *)anything : 0;

    vwm = vwm_get_instance();
    top = vk_deck_get_top(vwm->deck);
    focused = (top == VK_WIDGET(window));

    getmaxyx(canvas, y, x);

    if(vwm->attention == VK_WIDGET(window))
    {
        extra = A_BOLD;
        if(vwm->attention_phase)
        {
            pair = vdk_color_pair(COLOR_WHITE, COLOR_RED);
            vk_window_set_border_colors(window, COLOR_WHITE, COLOR_RED);
        }
        else
        {
            pair = vdk_color_pair(COLOR_BLACK, COLOR_YELLOW);
            vk_window_set_border_colors(window, COLOR_BLACK, COLOR_YELLOW);
        }
    }
    else if(focused)
    {
        pair = vdk_color_pair(COLOR_WHITE, COLOR_MAGENTA);
        extra = A_BOLD;
        vk_window_set_border_colors(window, COLOR_WHITE, COLOR_MAGENTA);
    }
    else
    {
        pair = vdk_color_pair(COLOR_BLACK, COLOR_CYAN);
        extra = A_NORMAL;
        vk_window_set_border_colors(window, COLOR_BLACK, COLOR_CYAN);
    }

    for(col = 0; col < x; col++)
    {
        mvwin_wch(canvas, 0, col, &cc);
        getcchar(&cc, wch, &attrs, &dummy, NULL);
        setcchar(&cc, wch, (attrs & A_ALTCHARSET) | extra, pair, NULL);
        mvwadd_wch(canvas, 0, col, &cc);

        mvwin_wch(canvas, y - 1, col, &cc);
        getcchar(&cc, wch, &attrs, &dummy, NULL);
        setcchar(&cc, wch, (attrs & A_ALTCHARSET) | extra, pair, NULL);
        mvwadd_wch(canvas, y - 1, col, &cc);
    }

    for(row = 1; row < y - 1; row++)
    {
        mvwin_wch(canvas, row, 0, &cc);
        getcchar(&cc, wch, &attrs, &dummy, NULL);
        setcchar(&cc, wch, (attrs & A_ALTCHARSET) | extra, pair, NULL);
        mvwadd_wch(canvas, row, 0, &cc);

        mvwin_wch(canvas, row, x - 1, &cc);
        getcchar(&cc, wch, &attrs, &dummy, NULL);
        setcchar(&cc, wch, (attrs & A_ALTCHARSET) | extra, pair, NULL);
        mvwadd_wch(canvas, row, x - 1, &cc);
    }

    wattron(canvas, COLOR_PAIR(pair) | extra);

    /* window controls, right-aligned with a two-column margin from the
       corner:  [v][X]__  (v = down arrow, 'v' on non-UTF-8).  Each button is
       three cells wide -- see the matching hit-test in poll_input_thd.c. */
    close_col = x - 2 - 3;          /* [X] ends two columns short of the corner */
    min_col   = close_col - 3;      /* [v] sits immediately left of [X] */

    mvwprintw(canvas, 0, close_col, "[X]");

    mvwaddch(canvas, 0, min_col, '[');
    if(vwm_has_utf8())
    {
        wch[0] = 0x2193;                        /* U+2193 DOWNWARDS ARROW */
        wch[1] = L'\0';
        setcchar(&cc, wch, extra, pair, NULL);
        mvwadd_wch(canvas, 0, min_col + 1, &cc);
    }
    else
        mvwaddch(canvas, 0, min_col + 1, 'v');
    mvwaddch(canvas, 0, min_col + 2, ']');

    snprintf(buf, sizeof(buf), "[%d x %d]", x - 2 - reserved, y - 2);
    len = strlen(buf);
    mvwprintw(canvas, y - 1, (x / 2) - (len / 2), "%s", buf);

    state = vk_widget_get_state(VK_WIDGET(window));
    if(!(state & VK_STATE_NORESIZE))
        mvwaddch(canvas, y - 1, x - 1, '*');

    wattroff(canvas, COLOR_PAIR(pair) | extra);
}

void
vwm_modules_preload(vwm_t *vwm)
{
    char            *module_dirs[] = { NULL, _VWM_SHARED_MODULES };
    char            *error_msg;
    int             array_sz;
    int             i;

    array_sz = sizeof(module_dirs) / sizeof(module_dirs[0]);
    module_dirs[0] = vwm_profile_mod_dir_get(vwm);

    for(i = 0;i < array_sz;i++)
    {
        error_msg = vwm_modules_load(module_dirs[i]);

        if(error_msg != NULL)
        {

            endwin();
            printf("[EE] Module loading failed\n\r");
            printf("%s\n\r", error_msg);
            exit(0);
        }
    }

	return;
}

int
vwm_exit(vk_widget_t *widget, void *anything)
{
    extern int  shutdown;

    (void)widget;
    (void)anything;

    shutdown = 1;

    return 0;
}

int
vwm_toggle_winman(vk_widget_t *widget, void *anything)
{
    (void)widget;
    (void)anything;

    vwm_panel_ON_KEYSTROKE(VWM_HOTKEY_WM, NULL);

    return 0;
}
