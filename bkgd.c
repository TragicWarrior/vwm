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
#include <unistd.h>
#include <wchar.h>
#include <langinfo.h>

#include "vwm.h"
#include "private.h"
#include "bkgd.h"

/* big-font hostname renderer, registered by the vwmfont module on load
   (NULL until then; the hostname falls back to the plain one-row label). */
static vwm_font_render_fn   g_font_render = NULL;
static vwm_font_apply_fn    g_font_apply  = NULL;
static vwm_font_free_fn     g_font_free   = NULL;

void
vwm_register_font_renderer(vwm_font_render_fn render,
    vwm_font_apply_fn apply, vwm_font_free_fn free_fn)
{
    g_font_render = render;
    g_font_apply  = apply;
    g_font_free   = free_fn;
}

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
    "Crosses",
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

    /* apply the pair as a separate short so bright (8-15) colors, whose
       pair numbers exceed 255, are not truncated by COLOR_PAIR */
    wattr_set(canvas, A_ALTCHARSET, pair, NULL);
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, ACS_CKBOARD);
    wattr_set(canvas, A_NORMAL, 0, NULL);
}

static void
_bkgd_render_dots_1(WINDOW *canvas, int width, int height, short pair)
{
    int i;

    wattr_set(canvas, A_NORMAL, pair, NULL);
    wmove(canvas, 0, 0);
    for(i = 0; i < width * height; i++)
        waddch(canvas, '.');
    wattr_set(canvas, A_NORMAL, 0, NULL);
}

/*
    Dots 2 -- 2x2 tile of (space, period) / (period, space).
    Effectively a dot where (x + y) is odd.
*/
static void
_bkgd_render_dots_2(WINDOW *canvas, int width, int height, short pair)
{
    int x, y;

    wattr_set(canvas, A_NORMAL, pair, NULL);
    for(y = 0; y < height; y++)
    {
        for(x = 0; x < width; x++)
        {
            chtype ch = ((y + x) & 1) ? '.' : ' ';
            mvwaddch(canvas, y, x, ch);
        }
    }
    wattr_set(canvas, A_NORMAL, 0, NULL);
}

/*
    Crosses -- 2x6 tile, a single '+' staggered between the two rows:
        row 0: '+' in the middle of the right half  (x % 6 == 4)
        row 1: '+' in the middle of the left half   (x % 6 == 1)
*/
static void
_bkgd_render_crosses(WINDOW *canvas, int width, int height, short pair)
{
    int x, y;

    wattr_set(canvas, A_NORMAL, pair, NULL);
    for(y = 0; y < height; y++)
    {
        for(x = 0; x < width; x++)
        {
            int    target = (y & 1) ? 1 : 4;
            chtype ch = ((x % 6) == target) ? '+' : ' ';
            mvwaddch(canvas, y, x, ch);
        }
    }
    wattr_set(canvas, A_NORMAL, 0, NULL);
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

/*
    Wallpaper backing-WINDOW cache, one slot per possible desktop.

    The wallpaper is static between user-driven changes (color, pattern,
    or terminal resize), but the libviper refresh loop calls our
    callback on every vk_screen_refresh.  Pre-render each desktop's
    wallpaper into its own off-screen WINDOW once and just blit it onto
    the surface canvas per refresh, replacing ~width*height per-cell
    ncurses calls with a single bulk copywin.

    Lifetime:
      - Lazy create:    the callback fills NULL slots on first use.
      - Resize:         geometry mismatch in the callback delwin's +
                        rebuilds.
      - Color/pattern:  vwm_invalidate_wallpaper_cache() called from
                        Settings change paths.
      - Surface remove: vwm_invalidate_wallpaper_cache() in the
                        vwm_apply_surface_count shrink branch.
      - Teleport:       vwm_invalidate_wallpaper_cache_all_orphan()
                        nulls without delwin (the WINDOWs are bound to
                        a SCREEN that's about to die; same intentional
                        leak as libviper's canvases -- see KLASSES.md
                        "Old ncurses WINDOWs are intentionally leaked
                        during teleport").
*/
static WINDOW *g_wallpaper_cache[VWM_MAX_DESKTOPS];

static void
_bkgd_paint_into(WINDOW *target, int surface_id, int width, int height)
{
    vwm_t       *vwm;
    short       fg;
    short       bg;
    short       pattern;
    short       pair;

    vwm = vwm_get_instance();

    fg = COLOR_BLACK;
    bg = COLOR_BLUE;
    pattern = VWM_WALLPAPER_STIPLE;
    if(vwm != NULL && surface_id >= 0 && surface_id < VWM_MAX_DESKTOPS)
    {
        fg = vwm->desktop_fg[surface_id];
        bg = vwm->desktop_color[surface_id];
        pattern = vwm->desktop_wallpaper[surface_id];
    }
    pair = vdk_color_pair(fg, bg);

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
            int i;
            wattr_set(target, A_NORMAL, pair, NULL);
            wmove(target, 0, 0);
            for(i = 0; i < width * height; i++) waddch(target, ' ');
            wattr_set(target, A_NORMAL, 0, NULL);
            break;
        }
        case VWM_WALLPAPER_SMALL_BRICKS:
            _bkgd_render_small_bricks(target, width, height, pair);
            break;
        case VWM_WALLPAPER_LARGE_BRICKS:
            _bkgd_render_large_bricks(target, width, height, pair);
            break;
        case VWM_WALLPAPER_DOTS_1:
            _bkgd_render_dots_1(target, width, height, pair);
            break;
        case VWM_WALLPAPER_DOTS_2:
            _bkgd_render_dots_2(target, width, height, pair);
            break;
        case VWM_WALLPAPER_CROSSES:
            _bkgd_render_crosses(target, width, height, pair);
            break;
        case VWM_WALLPAPER_STIPLE:
        default:
            _bkgd_render_stiple(target, width, height, pair);
            break;
    }
}

/*
    Big-font host name via the registered vwmfont renderer.  Renders the
    name into a cached vk_widget and blits its canvas into the bottom-left,
    bottom row at height-3 (one blank row above the status line).  Returns
    1 on success, 0 to fall back to the plain one-row label.  The widget is
    re-rendered only when the text/size/fill changes and recolored only
    when the colors change, so the steady-state cost is a single copywin.
*/
static int
_bkgd_draw_hostname_big(WINDOW *canvas, vwm_t *vwm, const char *host,
    int width, int height)
{
    static vk_widget_t  *cached = NULL;
    static int          c_font = -2, c_fill = -2;
    static short        c_fg = -2, c_bg = -2;
    static char         c_host[256] = "";
    WINDOW              *src;
    int                 W, H, top, left, right;

    if(c_font != vwm->hostname_font || c_fill != vwm->hostname_fill ||
       strcmp(c_host, host) != 0)
    {
        if(cached != NULL && g_font_free != NULL) g_font_free(cached);
        cached = g_font_render(host, vwm->hostname_font, vwm->hostname_fill);
        c_font = vwm->hostname_font;
        c_fill = vwm->hostname_fill;
        snprintf(c_host, sizeof(c_host), "%s", host);
        c_fg = -2;                  /* force a recolor */
    }
    if(cached == NULL) return 0;    /* render failed -> fall back */

    if(c_fg != vwm->hostname_fg || c_bg != vwm->hostname_bg)
    {
        if(g_font_apply != NULL)
            g_font_apply(cached, vwm->hostname_fg, vwm->hostname_bg);
        c_fg = vwm->hostname_fg;
        c_bg = vwm->hostname_bg;
    }

    src = vk_widget_get_canvas(cached);
    if(src == NULL) return 0;
    getmaxyx(src, H, W);

    left = 1;                       /* one column in from the left edge */
    top  = height - 2 - H;          /* bottom row at height-3 */
    if(top < 0) return 0;           /* too tall for this screen */

    right = left + W - 1;
    if(right > width - 1) right = width - 1;     /* clip to screen width */
    if(right < left) return 0;

    copywin(src, canvas, 0, 0, top, left, top + H - 1, right, FALSE);
    return 1;
}

/*
    Draw the host name in the desktop's bottom-left corner: one column in
    from the left edge, with one blank row between it and the bottom
    status line (status sits at height-1, so the name goes at height-3).
    Must be called AFTER the wallpaper is painted onto the canvas, or the
    wallpaper blit overwrites it.  Gated by the Settings "Show Hostname"
    option (off by default); colored by the "Hostname Colors" option,
    applied via wattr_set so bright (8-15) colors survive.
*/
static void
_bkgd_draw_hostname(WINDOW *canvas, int surface_id, int width, int height)
{
    static char     host[256] = "";
    vwm_t           *vwm;
    char            *dot;
    short           fg, bg, pair;
    int             row, col, len;

    (void)surface_id;

    vwm = vwm_get_instance();
    if(vwm == NULL || !vwm->show_hostname) return;

    if(host[0] == '\0')
    {
        if(gethostname(host, sizeof(host)) != 0) return;
        host[sizeof(host) - 1] = '\0';
        /* short name only -- drop any domain suffix */
        dot = strchr(host, '.');
        if(dot != NULL) *dot = '\0';
        if(host[0] == '\0') return;
    }

    if(height < 3 || width < 2) return;

    /* big-font path: a registered vwmfont renderer and a non-Basic size */
    if(vwm->hostname_font >= 0 && g_font_render != NULL)
    {
        if(_bkgd_draw_hostname_big(canvas, vwm, host, width, height))
            return;
        /* unavailable / didn't fit -- fall through to the plain label */
    }

    row = height - 3;       /* status at height-1, blank row at height-2 */
    col = 1;                /* one column in from the left edge */

    len = (int)strlen(host);
    if(col + len > width) len = width - col;
    if(len <= 0) return;

    fg = vwm->hostname_fg;
    bg = vwm->hostname_bg;
    if(fg < 0 || fg > 15) fg = COLOR_WHITE;
    if(bg < 0 || bg > 15) bg = COLOR_BLUE;
    pair = vdk_color_pair(fg, bg);

    wattr_set(canvas, A_BOLD, pair, NULL);
    mvwaddnstr(canvas, row, col, host, len);
    wattr_set(canvas, A_NORMAL, 0, NULL);
}

void
vwm_bkgd_simple_normal(vk_screen_t *screen, int surface_id, WINDOW *canvas)
{
    int         width, height;
    int         cw, ch;

    (void)screen;

    if(surface_id < 0 || surface_id >= VWM_MAX_DESKTOPS)
    {
        /* out-of-range slot -- paint directly, no caching */
        getmaxyx(canvas, height, width);
        _bkgd_paint_into(canvas, surface_id, width, height);
        _bkgd_draw_hostname(canvas, surface_id, width, height);
        return;
    }

    getmaxyx(canvas, height, width);

    /* resize check: if the cached window's size doesn't match the
       current canvas, drop it and rebuild at the new size.  same-SCREEN
       so delwin is safe. */
    if(g_wallpaper_cache[surface_id] != NULL)
    {
        getmaxyx(g_wallpaper_cache[surface_id], ch, cw);
        if(ch != height || cw != width)
            vwm_invalidate_wallpaper_cache(surface_id);
    }

    if(g_wallpaper_cache[surface_id] == NULL)
    {
        g_wallpaper_cache[surface_id] = newwin(height, width, 0, 0);
        if(g_wallpaper_cache[surface_id] == NULL)
        {
            /* newwin failed -- paint directly so we don't lose a frame */
            _bkgd_paint_into(canvas, surface_id, width, height);
            _bkgd_draw_hostname(canvas, surface_id, width, height);
            return;
        }
        _bkgd_paint_into(g_wallpaper_cache[surface_id],
            surface_id, width, height);
    }

    /* copywin in overwrite mode (FALSE) replaces every cell of the
       destination rectangle, blanks included -- identical effect to
       overwrite() but explicit about the rectangle and the mode. */
    overwrite(g_wallpaper_cache[surface_id], canvas);

    /* host name in the bottom-left -- drawn after the wallpaper blit so it
       is not overpainted (see _bkgd_draw_hostname). */
    _bkgd_draw_hostname(canvas, surface_id, width, height);
}

/*
    Drop a single surface's cached wallpaper.  delwin is safe -- callers
    invoke this only from the SCREEN that owns the WINDOW (Settings
    change, surface shrink, geometry-mismatch rebuild).
*/
void
vwm_invalidate_wallpaper_cache(int surface_id)
{
    if(surface_id < 0 || surface_id >= VWM_MAX_DESKTOPS) return;

    if(g_wallpaper_cache[surface_id] != NULL)
    {
        delwin(g_wallpaper_cache[surface_id]);
        g_wallpaper_cache[surface_id] = NULL;
    }
}

void
vwm_invalidate_wallpaper_cache_all(void)
{
    int i;
    for(i = 0; i < VWM_MAX_DESKTOPS; i++)
        vwm_invalidate_wallpaper_cache(i);
}

/*
    Teleport variant: the cached WINDOWs are bound to a SCREEN that's
    about to be torn down.  delwin on a different-SCREEN window
    corrupts ncurses internal state (same reason libviper leaks
    surface canvases during teleport -- see KLASSES.md).  Null the
    slots and let the WINDOWs leak with the dying SCREEN; next refresh
    after the new SCREEN is established lazily allocates fresh ones.
*/
void
vwm_invalidate_wallpaper_cache_all_orphan(void)
{
    int i;
    for(i = 0; i < VWM_MAX_DESKTOPS; i++)
        g_wallpaper_cache[i] = NULL;
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

/*
    Persist a wbkgdset value on the given surface so transient erases
    (between werase() and the next wallpaper/widget composite) show the
    desktop's color instead of black.  Uses just COLOR_PAIR with no
    attribute bits so the ncurses bkgd-OR doesn't bleed bold/reverse
    into any cell written to the surface.  libviper reapplies after
    teleport automatically (vk_screen_set_surface_bkgd persists the
    value).
*/
void
vwm_apply_desktop_bkgd(int surface_id)
{
    vwm_t   *vwm;
    short   bg;
    short   pair;

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->screen == NULL) return;
    if(surface_id < 0 || surface_id >= vwm->surface_count) return;

    bg   = vwm->desktop_color[surface_id];
    pair = vdk_color_pair(COLOR_BLACK, bg);

    vk_screen_set_surface_bkgd(vwm->screen, surface_id,
        ' ' | COLOR_PAIR(pair));
}

void
vwm_apply_desktop_bkgd_all(void)
{
    vwm_t   *vwm;
    int     i;

    vwm = vwm_get_instance();
    if(vwm == NULL) return;

    for(i = 0; i < vwm->surface_count; i++)
        vwm_apply_desktop_bkgd(i);
}
