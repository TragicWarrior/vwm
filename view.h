#ifndef _H_VWM_VIEW_
#define _H_VWM_VIEW_

/*
    The screen, published for viewers.

    A viewer is another program on this host (vwm-view, typically
    reached over ssh) that shows what is on this session's screen in a
    terminal of its own, without the session moving there.  It asks for
    this with the control op "view" and keeps that connection open.

    While at least one viewer is connected, vwm keeps a copy of its
    composed screen up to date in a file (viewfile.h) and
    tells each viewer, with a single byte on its connection, when the
    file has changed.  Viewers read the file themselves.  Nothing vwm
    sends a viewer is large and nothing waits for a viewer, so one that
    is slow or stuck cannot hold the screen up.

    With no viewer connected none of this runs.
*/

/* the most viewers at once */
#define VWM_VIEW_MAX            4

/*
    Take on a viewer whose control connection is `fd`.  On success the
    connection is vwm's to keep and close, the file's path is copied to
    `path` and the screen's size to *cols and *rows, and 0 is returned.
    Returns -1 when there is no room for another viewer or publishing
    could not be started; the connection is then still the caller's.
*/
int     vwm_view_add(int fd, char *path, size_t path_sz, int *cols, int *rows);

/*
    Once per scheduler step: let go of viewers that have left, and if
    any remain bring the file up to date and tell them when it changed.
*/
void    vwm_view_step(void);

/* how many viewers are connected */
int     vwm_view_count(void);

/* the session is ending: drop every viewer and remove the file */
void    vwm_view_shutdown(void);

#endif
