#include <stdlib.h>

#include <ncursesw/curses.h>

#include "screenshot_priv.h"

/*
    Read every cell of `win` into a grid of portable cells.  The decode
    of a cell -- line-drawing characters to Unicode, the colour pair to
    RGB, reverse and dim worked into the colours -- is libviper's
    vdk_cells_read_row, shared with anything else that needs a canvas's
    contents.
*/
scrshot_grid_t*
vwmscrshot_capture(WINDOW *win)
{
    scrshot_grid_t  *grid;
    int             rows, cols;
    int             y;

    if(win == NULL) return NULL;

    getmaxyx(win, rows, cols);
    if(rows <= 0 || cols <= 0) return NULL;

    grid = (scrshot_grid_t *)calloc(1, sizeof(scrshot_grid_t));
    if(grid == NULL) return NULL;

    grid->rows = rows;
    grid->cols = cols;
    grid->cells = (vdk_cell_t *)calloc((size_t)rows * cols,
        sizeof(vdk_cell_t));

    if(grid->cells == NULL)
    {
        free(grid);
        return NULL;
    }

    for(y = 0; y < rows; y++)
        vdk_cells_read_row(win, y, &grid->cells[y * cols], cols);

    return grid;
}

void
vwmscrshot_grid_free(scrshot_grid_t *grid)
{
    if(grid == NULL) return;

    free(grid->cells);
    free(grid);
}
