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
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wchar.h>
#include <langinfo.h>

#include "vwm.h"
#include "private.h"
#include "bkgd.h"

/*
    Cheap, cached "does the terminal speak UTF-8?" check.  Used to fall
    back the brick wallpapers (WACS box drawing) to Stiple (ACS_CKBOARD)
    when the term can't render wide characters.  Mirrors the detection
    in vwm_panel_init: locale CODESET must be UTF-8 and TERM must not
    be "linux" (the bare console).
*/
static bool
_bkgd_has_utf8(void)
{
    static int cached = -1;
    const char *term;

    if(cached >= 0) return cached != 0;

    cached = (strcmp(nl_langinfo(CODESET), "UTF-8") == 0) ? 1 : 0;
    term = getenv("TERM");
    if(term != NULL && strcmp(term, "linux") == 0) cached = 0;

    return cached != 0;
}

const char *vwm_wallpaper_names[VWM_WALLPAPER_COUNT] =
{
    "None",
    "Stiple",
    "Small Bricks",
    "Large Bricks",
    "Dots 1",
    "Dots 2",
};

/* host clipboard sync mode names (parallel to VWM_CLIPBOARD_* values) */
const char *vwm_clipboard_mode_names[VWM_CLIPBOARD_COUNT] =
{
    "Never",
    "OSC 52",
    "xclip",
    "Both",
};

/* ANSI color names exposed for Settings + JSON persistence */
const char *vwm_color_names[16] =
{
    "Black",    "Red",        "Green",     "Yellow",
    "Blue",     "Magenta",    "Cyan",      "White",
    "Br Black", "Br Red",     "Br Green",  "Br Yellow",
    "Br Blue",  "Br Magenta", "Br Cyan",   "Br White"
};

/*
    Build a cchar_t from a WACS_* source using the given color pair.
    The source's own wide chars are preserved; only the pair is
    swapped in (so e.g. WACS_HLINE keeps its '─' glyph but renders
    with our chosen fg/bg).
*/
static void
_bkgd_make_cc(cchar_t *dest, const cchar_t *src, short pair)
{
    wchar_t     wch[CCHARW_MAX];
    attr_t      attrs;
    short       dummy;

    getcchar(src, wch, &attrs, &dummy, NULL);
    setcchar(dest, wch, attrs, pair, NULL);
}

static void
_bkgd_render_stiple(WINDOW *canvas, int width, int height, short pair)
{
    int i;
    int colors = COLOR_PAIR(pair);

    wattron(canvas, colors | A_ALTCHARSET);
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, ACS_CKBOARD);
    wattroff(canvas, colors | A_ALTCHARSET);
}

static void
_bkgd_render_dots_1(WINDOW *canvas, int width, int height, short pair)
{
    int i;
    int colors = COLOR_PAIR(pair);

    wattron(canvas, colors);
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, '.');
    wattroff(canvas, colors);
}

/*
    Dots 2 -- 2x2 tile of (space, period) / (period, space).
    Effectively a dot where (x + y) is odd.
*/
static void
_bkgd_render_dots_2(WINDOW *canvas, int width, int height, short pair)
{
    int colors = COLOR_PAIR(pair);
    int x, y;

    wattron(canvas, colors);
    for(y = 0; y < height; y++)
    {
        for(x = 0; x < width; x++)
        {
            chtype ch = ((y + x) & 1) ? '.' : ' ';
            mvwaddch(canvas, y, x, ch);
        }
    }
    wattroff(canvas, colors);
}

/*
    Small Bricks -- 2x2 tile of alternating tees:
        row 0: BTEE TTEE  ('tee up' then 'tee down')
        row 1: TTEE BTEE  (inverted)
    Generalized: BTEE when (y%2) == (x%2), else TTEE.
*/
static void
_bkgd_render_small_bricks(WINDOW *canvas, int width, int height, short pair)
{
    cchar_t     cc_btee, cc_ttee;
    int         x, y;

    _bkgd_make_cc(&cc_btee, WACS_BTEE, pair);
    _bkgd_make_cc(&cc_ttee, WACS_TTEE, pair);

    for(y = 0; y < height; y++)
    {
        for(x = 0; x < width; x++)
        {
            cchar_t *cc = ((y & 1) == (x & 1)) ? &cc_btee : &cc_ttee;
            mvwadd_wch(canvas, y, x, cc);
        }
    }
}

/*
    Large Bricks -- 6x4 tile (each brick is 3 cells wide):

        ┴ ─ ─ ┬ ─ ─
        . . . │ . .
        ┬ ─ ─ ┴ ─ ─
        │ . . . . .

    Tees mirror Small Bricks's rhythm but offset by 3 columns instead
    of 2.  Horizontal lines connect tees on a row.  Vertical lines
    only appear between tees whose pips face each other: at (1,3)
    inside the tile (┬ over ┴), and at (3,0) across the seam (this
    tile's ┬ over the next-row tile's ┴).
*/
static void
_bkgd_render_large_bricks(WINDOW *canvas, int width, int height, short pair)
{
    /* per-cell tile lookup -- 0 = space, 1 = HLINE, 2 = VLINE,
       3 = BTEE, 4 = TTEE */
    static const unsigned char tile[4][6] =
    {
        { 3, 1, 1, 4, 1, 1 },
        { 0, 0, 0, 2, 0, 0 },
        { 4, 1, 1, 3, 1, 1 },
        { 2, 0, 0, 0, 0, 0 },
    };
    cchar_t     cc_h, cc_v, cc_btee, cc_ttee, cc_sp;
    wchar_t     space[2] = { L' ', L'\0' };
    int         colors = COLOR_PAIR(pair);
    int         x, y;

    _bkgd_make_cc(&cc_h,    WACS_HLINE, pair);
    _bkgd_make_cc(&cc_v,    WACS_VLINE, pair);
    _bkgd_make_cc(&cc_btee, WACS_BTEE,  pair);
    _bkgd_make_cc(&cc_ttee, WACS_TTEE,  pair);
    setcchar(&cc_sp, space, 0, pair, NULL);

    wattron(canvas, colors);
    for(y = 0; y < height; y++)
    {
        for(x = 0; x < width; x++)
        {
            const cchar_t *cc;
            switch(tile[y & 3][x % 6])
            {
                case 1:  cc = &cc_h;    break;
                case 2:  cc = &cc_v;    break;
                case 3:  cc = &cc_btee; break;
                case 4:  cc = &cc_ttee; break;
                default: cc = &cc_sp;   break;
            }
            mvwadd_wch(canvas, y, x, cc);
        }
    }
    wattroff(canvas, colors);
}

void
vwm_bkgd_simple_normal(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    vwm_t       *vwm;
    short       pair;
    short       bg;
    short       pattern;
    int         width, height;

    (void)screen;

    vwm = vwm_get_instance();

    getmaxyx(canvas, height, width);

    /* per-surface desktop color + wallpaper pattern picked by the user
       in Settings; render the chosen tile with black FG over the
       chosen color as BG */
    bg = COLOR_BLUE;
    pattern = VWM_WALLPAPER_STIPLE;
    if(vwm != NULL && surface_id >= 0 && surface_id < VWM_MAX_DESKTOPS)
    {
        bg = vwm->desktop_color[surface_id];
        pattern = vwm->desktop_wallpaper[surface_id];
    }
    pair = vdk_color_pair(COLOR_BLACK, bg);

    /* fall back to Stiple if the saved pattern needs wide-char box
       drawing but the terminal doesn't support UTF-8 */
    if(!_bkgd_has_utf8())
    {
        if(pattern == VWM_WALLPAPER_SMALL_BRICKS ||
           pattern == VWM_WALLPAPER_LARGE_BRICKS)
            pattern = VWM_WALLPAPER_STIPLE;
    }

    switch(pattern)
    {
        case VWM_WALLPAPER_NONE:
        {
            /* solid fill: just the desktop color, no overlay glyph */
            int colors = COLOR_PAIR(pair);
            int i;
            wattron(canvas, colors);
            wmove(canvas, 0, 0);
            for(i = 0; i < width * height; i++) waddch(canvas, ' ');
            wattroff(canvas, colors);
            break;
        }
        case VWM_WALLPAPER_SMALL_BRICKS:
            _bkgd_render_small_bricks(canvas, width, height, pair);
            break;
        case VWM_WALLPAPER_LARGE_BRICKS:
            _bkgd_render_large_bricks(canvas, width, height, pair);
            break;
        case VWM_WALLPAPER_DOTS_1:
            _bkgd_render_dots_1(canvas, width, height, pair);
            break;
        case VWM_WALLPAPER_DOTS_2:
            _bkgd_render_dots_2(canvas, width, height, pair);
            break;
        case VWM_WALLPAPER_STIPLE:
        default:
            _bkgd_render_stiple(canvas, width, height, pair);
            break;
    }
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
