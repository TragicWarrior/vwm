#ifndef _VWMSCRSHOT_H_
#define _VWMSCRSHOT_H_

#include <stdint.h>
#include <wchar.h>

#include <ncursesw/curses.h>

/*
    distinct return codes used by the renderer and PNG writer.  every
    code is non-zero so that SCRSHOT_OK (0) is the lone success value.
*/
enum
{
    SCRSHOT_OK          = 0,
    SCRSHOT_ERR_FONT    = 1,    /* FreeType init / face / sizing failed */
    SCRSHOT_ERR_GLYPH   = 2,    /* a required glyph could not be loaded */
    SCRSHOT_ERR_ALLOC   = 3,    /* memory allocation failed             */
    SCRSHOT_ERR_PNG     = 4      /* PNG file open / write failed         */
};

/* one decoded terminal cell */
typedef struct
{
    wchar_t wch;        /* primary wide character (space for blanks)    */
    attr_t  attrs;      /* attribute mask (no color bits)               */
    short   pair;       /* ncurses color pair number                    */
}
scrshot_cell_t;

/* a captured rectangular grid of cells (row-major) */
typedef struct
{
    int             rows;
    int             cols;
    scrshot_cell_t  *cells;
}
scrshot_grid_t;

/* rendering configuration */
typedef struct
{
    const char  *font_path;     /* TTF/OTF path for the regular face    */
    const char  *bold_path;     /* bold face path, or NULL to emulate   */
    int         font_px;        /* pixel size to load the face at       */
}
scrshot_cfg_t;

/* capture.c -------------------------------------------------------------- */

/* read every cell of win into a freshly allocated grid (NULL on failure) */
scrshot_grid_t* vwmscrshot_capture(WINDOW *win);

/* release a grid created by vwmscrshot_capture() */
void            vwmscrshot_grid_free(scrshot_grid_t *grid);

/* render.c --------------------------------------------------------------- */

/*
    render grid into a freshly allocated RGB (3 bytes/pixel) buffer.
    on success returns SCRSHOT_OK and stores the buffer (caller frees with
    free()) plus its dimensions.  on failure returns a SCRSHOT_ERR_* code
    and leaves *out_rgb untouched.
*/
int vwmscrshot_render(const scrshot_grid_t *grid, const scrshot_cfg_t *cfg,
        uint8_t **out_rgb, int *out_w, int *out_h);

/* pngwrite.c ------------------------------------------------------------- */

/* write an RGB buffer to path as a PNG; SCRSHOT_OK or SCRSHOT_ERR_*. */
int vwmscrshot_write_png(const char *path, const uint8_t *rgb,
        int width, int height);

#endif
