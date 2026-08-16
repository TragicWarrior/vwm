#ifndef _H_VWM_SCREENSHOT_PRIV_
#define _H_VWM_SCREENSHOT_PRIV_

#include <stdint.h>
#include <wchar.h>

#include <ncursesw/curses.h>

#include "screenshot.h"

/* one decoded terminal cell */
typedef struct
{
    wchar_t wch;
    attr_t  attrs;
    short   pair;
}
scrshot_cell_t;

typedef struct
{
    int             rows;
    int             cols;
    scrshot_cell_t  *cells;
}
scrshot_grid_t;

typedef struct
{
    const char  *font_path;
    const char  *bold_path;
    int         font_px;
}
scrshot_cfg_t;

scrshot_grid_t* vwmscrshot_capture(WINDOW *win);
void            vwmscrshot_grid_free(scrshot_grid_t *grid);

int vwmscrshot_render(const scrshot_grid_t *grid, const scrshot_cfg_t *cfg,
        uint8_t **out_rgb, int *out_w, int *out_h);

int vwmscrshot_write_png(const char *path, const uint8_t *rgb,
        int width, int height);

#endif
