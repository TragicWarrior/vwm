#ifndef _H_VWMFONT_
#define _H_VWMFONT_

/*
    vwmfont -- render a UTF-8 string as large "pixel-art" text using a
    Terminus PSF console font as the glyph source.  Each *on* pixel of a
    glyph becomes one filled terminal cell, each *off* pixel a blank cell.
    The result is painted into a freshly created vk_widget_t sized exactly
    to fit, which vwmfont_render() returns.  See vwmfont-spec.md.
*/

#include <ncursesw/curses.h>     /* cchar_t, used by vdk.h */
#include <vdk.h>

/* The nine Terminus grids, with weights where they exist:
   normal (n) and bold (b) for all but 6x12 (n only); plus CRT/VGA-bold
   (v) for 8x14 and 8x16.  The token mirrors the upstream console name:
   "1" + pixel-height + weight (e.g. ter-114n = 8x14 normal). */
typedef enum
{
    VWMFONT_112N = 0,   /*  6x12  */
    VWMFONT_114N,       /*  8x14  */
    VWMFONT_114B,
    VWMFONT_114V,
    VWMFONT_116N,       /*  8x16  */
    VWMFONT_116B,
    VWMFONT_116V,
    VWMFONT_118N,       /* 10x18  */
    VWMFONT_118B,
    VWMFONT_120N,       /* 10x20  */
    VWMFONT_120B,
    VWMFONT_122N,       /* 11x22  */
    VWMFONT_122B,
    VWMFONT_124N,       /* 12x24  */
    VWMFONT_124B,
    VWMFONT_128N,       /* 14x28  */
    VWMFONT_128B,
    VWMFONT_132N,       /* 16x32  */
    VWMFONT_132B,
    VWMFONT_SIZE_COUNT
}
vwmfont_size_t;

/* On-pixel fill.  FULLBLOCK is U+2588 via a wide-char cchar_t, with a
   transparent reverse-space fallback when U+2588 is not usable (so the
   user just sees a solid block either way).  O and X use the literal
   ASCII glyphs. */
typedef enum
{
    VWMFONT_FILL_FULLBLOCK = 0,
    VWMFONT_FILL_O,
    VWMFONT_FILL_X,
    VWMFONT_FILL_COUNT
}
vwmfont_fill_t;

/* Optional: locate + parse fonts once and cache them.  font_dir NULL =
   search $VWMFONT_DIR then the standard console-font directories.  If
   never called, vwmfont_render() lazily loads on first use.  Returns 0
   on success, -1 on failure. */
int             vwmfont_init(const char *font_dir);
void            vwmfont_shutdown(void);

/* Render utf8_text (embedded '\n' starts a new glyph row) at the given
   grid with the given on-pixel fill.  Returns a newly allocated
   vk_widget_t sized exactly to the text, or NULL on any failure (font
   missing, parse error, allocation failure, empty/whitespace input,
   out-of-range dimensions).  The caller applies fg/bg colors to the
   returned widget. */
vk_widget_t*    vwmfont_render(const char *utf8_text, vwmfont_size_t size,
                    vwmfont_fill_t fill);

/* Apply foreground/background to a rendered widget: on-pixels show fg,
   off-pixels show bg.  Call after vwmfont_render, and again to recolor.
   Correct for all fill modes (incl. the reverse-space fallback). */
void            vwmfont_apply_colors(vk_widget_t *widget, short fg, short bg);

/* Free a widget returned by vwmfont_render (its render metadata, then the
   widget itself).  Use this instead of vk_widget_destroy. */
void            vwmfont_free_widget(vk_widget_t *widget);

/* Human-readable token for a size (e.g. "ter-114n") -- handy for the
   Settings list.  Returns "" for out-of-range. */
const char*     vwmfont_size_token(vwmfont_size_t size);

#endif
