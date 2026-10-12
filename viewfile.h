#ifndef _H_VWM_VIEWFILE_
#define _H_VWM_VIEWFILE_

/*
    A screen kept up to date in a file, for another program on the same
    host to show.

    vwm (the writer) owns the screen and writes it out as libviper's
    portable cells; a viewer (the reader) opens the file and reads the
    cells back.  The two share only the file: the writer writes whether
    or not anyone is reading, so a reader that is slow, stopped or gone
    costs it nothing, and it never waits for one.  How a reader learns
    that something changed is not this file's business (vwm tells its
    viewers over their control connection: see view.h).

    The file is created readable by its owner alone.  Both halves are
    in one source file so that vwm and the viewer cannot disagree about
    the layout.
*/

#include <ncursesw/curses.h>

#include <vdk.h>

typedef struct  _vwm_viewfile_writer_s  vwm_viewfile_writer_t;
typedef struct  _vwm_viewfile_reader_s  vwm_viewfile_reader_t;

/* start writing to `path`.  whatever is at that path is replaced.  the
   file is empty of cells until the first update.  NULL on failure */
vwm_viewfile_writer_t*  vwm_viewfile_writer_create(const char *path);

/* bring the file up to date with `win` (any WINDOW; its size may
   differ from last time).  only rows that changed are written.
   returns the number of rows written -- 0 when nothing had changed,
   which is the time not to bother anyone -- or -1 on failure */
int                     vwm_viewfile_writer_update(
                            vwm_viewfile_writer_t *writer, WINDOW *win);

/* stop writing and remove the file */
void                    vwm_viewfile_writer_destroy(
                            vwm_viewfile_writer_t *writer);

/* open a published screen.  NULL when there is none at `path` */
vwm_viewfile_reader_t*  vwm_viewfile_reader_create(const char *path);

/* look for something new.  returns 1 when a newer screen was read in,
   0 when the one held is still current (or the writer was in the
   middle of a change: ask again), and -1 when the writer has stopped
   or the file is not a published screen */
int                     vwm_viewfile_reader_poll(
                            vwm_viewfile_reader_t *reader);

/* the size of the screen held; -1 before the first successful poll */
int                     vwm_viewfile_reader_get_size(
                            vwm_viewfile_reader_t *reader, int *cols,
                            int *rows);

/* row `y` of the screen held, `cols` cells long, or NULL.  valid until
   the next poll that returns 1 */
const vdk_cell_t*       vwm_viewfile_reader_get_row(
                            vwm_viewfile_reader_t *reader, int y);

void                    vwm_viewfile_reader_destroy(
                            vwm_viewfile_reader_t *reader);

#endif
