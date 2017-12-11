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

void
vwm_bkgd_simple_normal(int screen_id)
{
    WINDOW      *wallpaper;
    WINDOW      *screen_wnd;
    short       color = 0;
	int		    width, height;
	char		version_str[32];

#ifdef _VIPER_WIDE
	cchar_t		bg_char;
	wchar_t		ch[][2] = { {0x0020,0x0000},
						    {0x002e,0x0000} };
#else
    chtype		    ch =        ACS_CKBOARD;
    chtype          attr =      A_ALTCHARSET;
    unsigned int    fg =        COLOR_BLACK;
    unsigned int    bg =        COLOR_BLUE;
#endif

    if(screen_id == -1) screen_id = CURRENT_SCREEN_ID;

    wallpaper = viper_screen_get_wallpaper(screen_id);
    if(wallpaper == NULL) return;

    screen_wnd = viper_get_screen_window(screen_id);

    getmaxyx(screen_wnd, height, width);

    // viper_wresize_abs(bkgd_window, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
    wresize(wallpaper, height, width);

    color = viper_color_pair(fg, bg);

#ifdef _VIPER_WIDE
	setcchar(&bg_char, ch, 0, 0, NULL);
	window_fill(wallpaper, &bg_char, color, A_NORMAL);
#else
    window_fill(wallpaper, ch, color, attr);
#endif

    getmaxyx(wallpaper, height, width);
    color = viper_color_pair(COLOR_BLACK, COLOR_WHITE);
	sprintf(version_str, " VWM %s ", VWM_VERSION);
	wattron(wallpaper, COLOR_PAIR(color));
	mvwprintw(wallpaper, height - 1, width - (strlen(version_str)),
        version_str);
	wattron(wallpaper, A_NORMAL);

    overwrite(wallpaper, screen_wnd);

	return;
}

void
vwm_bkgd_simple_winman(int screen_id)
{
    WINDOW      *wallpaper;
    WINDOW      *screen_wnd;
    short       color = 0;
	int		    width, height;
	char		version_str[32];

#ifdef _VIPER_WIDE
	cchar_t		bg_char;
	wchar_t		ch[][2] = { {0x0020,0x0000},
						    {0x002e,0x0000} };
#else
    chtype		    ch =        '.';
    chtype          attr =      A_NORMAL;
    unsigned int    fg =        COLOR_BLACK;
    unsigned int    bg =        COLOR_WHITE;
#endif

    if(screen_id == -1) screen_id = CURRENT_SCREEN_ID;

    wallpaper = viper_screen_get_wallpaper(screen_id);
    if(wallpaper == NULL) return;

    screen_wnd = viper_get_screen_window(screen_id);

    getmaxyx(screen_wnd, height, width);

    // viper_wresize_abs(bkgd_window, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
    wresize(wallpaper, height, width);

    color = viper_color_pair(fg, bg);

#ifdef _VIPER_WIDE
	setcchar(&bg_char, ch, 0, 0, NULL);
	window_fill(wallpaper, &bg_char, color, A_NORMAL);
#else
    window_fill(wallpaper, ch, color, attr);
#endif

    getmaxyx(wallpaper, height, width);
    color = viper_color_pair(COLOR_BLACK, COLOR_WHITE);
	sprintf(version_str, " VWM %s ", VWM_VERSION);
	wattron(wallpaper, COLOR_PAIR(color));
	mvwprintw(wallpaper, height - 1, width - (strlen(version_str)),
        version_str);
	wattron(wallpaper, A_NORMAL);

    overwrite(wallpaper, screen_wnd);

	return;
}

int
vwm_bkgd_bricks(WINDOW *bkgd_window, void *arg)
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

    getmaxyx(CURRENT_SCREEN, height, width);
    wresize(bkgd_window, height, width);

    // viper_wresize_abs(bkgd_window, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
    wattroff(bkgd_window, A_REVERSE);

    getmaxyx(bkgd_window, height, width);
    cell_count = width * height;

    if(idx == 0)
    {
        color = viper_color_pair(COLOR_RED,COLOR_WHITE);

        for(i = 0;i < cell_count;i++)
        {
            x = i % width;
            y = (int)(i / width);

            wmove(bkgd_window, y, x);
            waddch(bkgd_window,
            brick[y % SPRITE_ROWS(brick)][x % SPRITE_COLS(brick)] | COLOR_PAIR(color));
        }
    }

    if(idx == 1)
    {
        color = viper_color_pair(COLOR_BLACK,COLOR_WHITE);
#ifdef _VIPER_WIDE
        setcchar(&bg_char, ch[idx], 0, 0, NULL);
        window_fill(bkgd_window, &bg_char, color, A_NORMAL);
#else
        window_fill(bkgd_window, ch[idx], color, A_NORMAL);
#endif
    }

    getmaxyx(bkgd_window, height, width);
    color = viper_color_pair(COLOR_BLACK, COLOR_WHITE);
    sprintf(version_str, " VWM %s ", VWM_VERSION);
    wattron(bkgd_window, COLOR_PAIR(color));
    mvwprintw(bkgd_window, height - 1, width - (strlen(version_str)),
        version_str);
    wattron(bkgd_window, A_NORMAL);

    return 0;
}
