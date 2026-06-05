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

#include <inttypes.h>

#include "bkgd.h"

void
vwm_bkgd_simple_normal(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    short       color;
    int         width, height;
    int         i;

    (void)screen;
    (void)surface_id;

    getmaxyx(canvas, height, width);

    color = vdk_color_pair(COLOR_BLACK, COLOR_BLUE);
    wattron(canvas, COLOR_PAIR(color) | A_ALTCHARSET);
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, ACS_CKBOARD);
    wattroff(canvas, COLOR_PAIR(color) | A_ALTCHARSET);
}

void
vwm_bkgd_simple_winman(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    short       color;
    int         width, height;
    int         i;

    (void)screen;
    (void)surface_id;

    getmaxyx(canvas, height, width);

    color = vdk_color_pair(COLOR_BLACK, COLOR_WHITE);
    wattron(canvas, COLOR_PAIR(color));
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, '.');
    wattroff(canvas, COLOR_PAIR(color));
}
