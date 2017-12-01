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


#include "vwm.h"
#include "bkgd.h"
#include "mainmenu.h"
#include "private.h"

#define  BRICK    (' ' | A_REVERSE)
#define  MORTAR   (' ' | A_NORMAL)

int
vwm_bkgd_simple(vwnd_t *vwnd, void *arg)
{
	uintmax_t	idx;
    short       color = 0;
	int		    width, height;
	char		version_str[32];

#ifdef _VIPER_WIDE
	cchar_t		bg_char;
	wchar_t		ch[][2] = { {0x0020,0x0000},
						    {0x002e,0x0000} };
#else
    chtype		    ch[] =      {ACS_CKBOARD,   '.'};
    chtype          attr[] =    {A_ALTCHARSET,  A_NORMAL};
    unsigned int    fg[] =      {COLOR_BLACK,    COLOR_BLACK};
    unsigned int    bg[] =      {COLOR_BLUE,    COLOR_WHITE};
#endif

   /* TODO:  gcc warning...
      warning: cast from pointer to integer of different size
   */
	idx = (uintmax_t)arg;

    viper_wresize_abs(vwnd, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);

    color = viper_color_pair(fg[idx], bg[idx]);

#ifdef _VIPER_WIDE
	setcchar(&bg_char, ch[idx], 0, 0, NULL);
	window_fill(VWINDOW(vnd), &bg_char, color, A_NORMAL);
#else
    window_fill(VWINDOW(vwnd), ch[idx], color, attr[idx]);
#endif

    getmaxyx(VWINDOW(vwnd), height, width);
    color = viper_color_pair(COLOR_BLACK, COLOR_WHITE);
	sprintf(version_str, " VWM %s ", VWM_VERSION);
	wattron(VWINDOW(vwnd), COLOR_PAIR(color));
	mvwprintw(VWINDOW(vwnd), height - 1, width - (strlen(version_str)),
        version_str);
	wattron(VWINDOW(vwnd), A_NORMAL);

	return 0;
}

int
vwm_bkgd_bricks(vwnd_t *vwnd, void *arg)
{
    uintmax_t       idx;
    char            version_str[32];
    short           color = 0;
    int             width, height;
    int             cell_count;
    int             x, y;
    int             i;
    static chtype   brick[6][10] = {
    {MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR},
    {BRICK,BRICK,MORTAR,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK},
    {BRICK,BRICK,MORTAR,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK},
    {MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR,MORTAR},
    {BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,MORTAR,BRICK,BRICK},
    {BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,BRICK,MORTAR,BRICK,BRICK}};

#ifdef _VIPER_WIDE
    cchar_t          bg_char;
    wchar_t          ch[][2] = { {0x0020, 0x0000},
                                {0x002e, 0x0000} };
#else
    chtype           ch[] = {' ', '.'};
#endif

    /*
        TODO:  gcc warning...
        warning: cast from pointer to integer of different size
    */
    idx = (uintmax_t)arg;

    viper_wresize_abs(vwnd, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
    wattroff(VWINDOW(vwnd), A_REVERSE);

    getmaxyx(VWINDOW(vwnd), height, width);
    cell_count = width * height;

    if(idx == 0)
    {
        color = viper_color_pair(COLOR_RED,COLOR_WHITE);

        for(i = 0;i < cell_count;i++)
        {
            x = i % width;
            y = (int)(i / width);

            wmove(VWINDOW(vwnd), y, x);
            waddch(VWINDOW(vwnd),
            brick[y % SPRITE_ROWS(brick)][x % SPRITE_COLS(brick)] | COLOR_PAIR(color));
        }
    }

    if(idx == 1)
    {
        color = viper_color_pair(COLOR_BLACK,COLOR_WHITE);
#ifdef _VIPER_WIDE
        setcchar(&bg_char, ch[idx], 0, 0, NULL);
        window_fill(VWINDOW(vwnd), &bg_char, color, A_NORMAL);
#else
        window_fill(VWINDOW(vwnd), ch[idx], color, A_NORMAL);
#endif
    }

    getmaxyx(VWINDOW(vwnd), height, width);
    color = viper_color_pair(COLOR_BLACK, COLOR_WHITE);
    sprintf(version_str, " VWM %s ", VWM_VERSION);
    wattron(VWINDOW(vwnd), COLOR_PAIR(color));
    mvwprintw(VWINDOW(vwnd), height - 1, width - (strlen(version_str)),
        version_str);
    wattron(VWINDOW(vwnd), A_NORMAL);

    return 0;
}
