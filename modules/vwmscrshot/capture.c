#include <stdlib.h>

#include <ncursesw/curses.h>

#include "scrshot.h"

/*
    Cells drawn with A_ALTCHARSET store a VT100 line-graphics key (e.g. 'q'
    for a horizontal line, 'a' for the checkerboard) rather than the glyph
    itself.  Translate that key to the matching Unicode code point so the
    renderer draws the intended box-drawing / block character instead of a
    literal Latin letter.
*/
static wchar_t
acs_to_unicode(wchar_t key)
{
    switch(key)
    {
        case 'a': return 0x2592;    /* checkerboard (CKBOARD)        */
        case 'h': return 0x2592;    /* board of squares (BOARD)      */
        case '0': return 0x2588;    /* solid block (BLOCK)           */
        case 'q': return 0x2500;    /* horizontal line (HLINE)       */
        case 'x': return 0x2502;    /* vertical line (VLINE)         */
        case 'l': return 0x250C;    /* upper-left corner (ULCORNER)  */
        case 'k': return 0x2510;    /* upper-right corner (URCORNER) */
        case 'm': return 0x2514;    /* lower-left corner (LLCORNER)  */
        case 'j': return 0x2518;    /* lower-right corner (LRCORNER) */
        case 't': return 0x251C;    /* left tee (LTEE)               */
        case 'u': return 0x2524;    /* right tee (RTEE)              */
        case 'v': return 0x2534;    /* bottom tee (BTEE)             */
        case 'w': return 0x252C;    /* top tee (TTEE)                */
        case 'n': return 0x253C;    /* cross / plus (PLUS)           */
        case '`': return 0x25C6;    /* diamond (DIAMOND)             */
        case 'f': return 0x00B0;    /* degree (DEGREE)               */
        case 'g': return 0x00B1;    /* plus/minus (PLMINUS)          */
        case ',': return 0x2190;    /* left arrow (LARROW)           */
        case '+': return 0x2192;    /* right arrow (RARROW)          */
        case '.': return 0x2193;    /* down arrow (DARROW)           */
        case '-': return 0x2191;    /* up arrow (UARROW)             */
        case '~': return 0x00B7;    /* bullet (BULLET)               */
        case 'o': return 0x23BA;    /* scan line 1 (S1)              */
        case 'p': return 0x23BB;    /* scan line 3 (S3)              */
        case 'r': return 0x23BC;    /* scan line 7 (S7)              */
        case 's': return 0x23BD;    /* scan line 9 (S9)              */
        case 'y': return 0x2264;    /* less than or equal (LEQUAL)   */
        case 'z': return 0x2265;    /* greater than or equal (GEQUAL)*/
        case '{': return 0x03C0;    /* pi (PI)                       */
        case '|': return 0x2260;    /* not equal (NEQUAL)            */
        case '}': return 0x00A3;    /* pound sterling (STERLING)     */
        default:  return key;       /* unknown: render the key as-is */
    }
}

scrshot_grid_t*
vwmscrshot_capture(WINDOW *win)
{
    scrshot_grid_t  *grid;
    int             rows, cols;
    int             y, x;

    if(win == NULL) return NULL;

    getmaxyx(win, rows, cols);
    if(rows <= 0 || cols <= 0) return NULL;

    grid = (scrshot_grid_t *)calloc(1, sizeof(scrshot_grid_t));
    if(grid == NULL) return NULL;

    grid->rows = rows;
    grid->cols = cols;
    grid->cells = (scrshot_cell_t *)calloc((size_t)rows * cols,
        sizeof(scrshot_cell_t));

    if(grid->cells == NULL)
    {
        free(grid);
        return NULL;
    }

    for(y = 0; y < rows; y++)
    {
        for(x = 0; x < cols; x++)
        {
            cchar_t         cc;
            wchar_t         wbuf[CCHARW_MAX + 1];
            wchar_t         base;
            attr_t          attrs = 0;
            short           pair = 0;
            scrshot_cell_t  *cell = &grid->cells[y * cols + x];

            if(mvwin_wch(win, y, x, &cc) != OK)
            {
                cell->wch = L' ';
                continue;
            }

            wbuf[0] = 0;

            if(getcchar(&cc, wbuf, &attrs, &pair, NULL) != OK)
                wbuf[0] = L' ';

            base = (wbuf[0] == 0) ? L' ' : wbuf[0];

            if(attrs & A_ALTCHARSET)
                base = acs_to_unicode(base);

            cell->wch = base;
            cell->attrs = attrs & ~A_ALTCHARSET;
            cell->pair = pair;
        }
    }

    return grid;
}

void
vwmscrshot_grid_free(scrshot_grid_t *grid)
{
    if(grid == NULL) return;

    free(grid->cells);
    free(grid);
}
