/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007-2026 Bryan Christ <bryan.christ@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "sched.h"
#include "view.h"
#include "viewfile.h"

/* the file is brought up to date no more often than this.  the
   scheduler steps at least ten times a second (the clock), so a change
   that lands inside the interval goes out at most a tick late */
#define VWM_VIEW_MIN_INTERVAL_MS    50

/* the control connections of the viewers; -1 marks a free slot */
static int              view_fds[VWM_VIEW_MAX] = { -1, -1, -1, -1 };
static int              view_count = 0;

static vwm_viewfile_writer_t  *view_pub = NULL;
static char             view_path[PATH_MAX];

/* when the file was last brought up to date, in milliseconds */
static long long        view_last_ms = 0;

/* milliseconds on a clock that only goes forward */
static long long
view_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
    Where the file goes: the user's runtime directory when there is one
    (it is in memory and cleared at logout), else beside the control
    socket.  The session's pid keeps two sessions apart.
*/
static void
view_make_path(char *buf, size_t n)
{
    const char  *dir;
    const char  *home;

    dir = getenv("XDG_RUNTIME_DIR");
    if(dir != NULL && dir[0] == '/')
    {
        snprintf(buf, n, "%s/vwm-view.%d", dir, (int)getpid());
        return;
    }

    home = getenv("HOME");
    if(home == NULL || home[0] == '\0') home = "/tmp";

    snprintf(buf, n, "%s/.config/vwm/view.%d", home, (int)getpid());
}

/* stop publishing and remove the file */
static void
view_stop_publishing(void)
{
    if(view_pub == NULL) return;

    vwm_viewfile_writer_destroy(view_pub);
    view_pub = NULL;
}

/* let viewer slot `i` go */
static void
view_drop(int i)
{
    extern vwm_sched_t  *sched;

    if(view_fds[i] < 0) return;

    vwm_sched_wake_fd_del(sched, view_fds[i]);
    close(view_fds[i]);
    view_fds[i] = -1;
    view_count--;

    /* the last one out: nobody to publish for */
    if(view_count == 0) view_stop_publishing();
}

/*
    Look at each viewer's connection.  A viewer sends nothing yet, so
    anything readable is either it closing (the program ended, or its
    ssh link went away) or bytes this version does not know what to do
    with, which are read and dropped so the connection does not stay
    readable and keep the scheduler awake.
*/
static void
view_check_viewers(void)
{
    char    junk[256];
    ssize_t n;
    int     i;

    for(i = 0; i < VWM_VIEW_MAX; i++)
    {
        if(view_fds[i] < 0) continue;

        n = recv(view_fds[i], junk, sizeof(junk), MSG_DONTWAIT);

        /* still there */
        if(n > 0) continue;
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
            || errno == EINTR))
            continue;

        view_drop(i);
    }
}

/*
    Tell every viewer the file has changed: one byte each, never
    waiting.  A viewer that has not read the last one has a full buffer
    or is about to find out anyway; the byte is dropped, which loses
    nothing, because it carries no information beyond "look again".
*/
static void
view_nudge(void)
{
    ssize_t n;
    int     i;

    for(i = 0; i < VWM_VIEW_MAX; i++)
    {
        if(view_fds[i] < 0) continue;

        n = send(view_fds[i], "\n", 1, MSG_DONTWAIT | MSG_NOSIGNAL);

        /* gone: the connection is broken, not merely full */
        if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK
            && errno != EINTR)
            view_drop(i);
    }
}

int
vwm_view_add(int fd, char *path, size_t path_sz, int *cols, int *rows)
{
    extern vwm_sched_t  *sched;
    vwm_t               *vwm = vwm_get_instance();
    WINDOW              *win;
    int                 slot = -1;
    int                 i;

    if(fd < 0 || vwm == NULL || vwm->screen == NULL) return -1;

    for(i = 0; i < VWM_VIEW_MAX; i++)
    {
        if(view_fds[i] < 0)
        {
            slot = i;
            break;
        }
    }

    if(slot < 0) return -1;

    win = vk_screen_get_window(vwm->screen);
    if(win == NULL) return -1;

    /* the first viewer starts the publishing, and the file holds the
       screen as it is now before the viewer is told where to look */
    if(view_pub == NULL)
    {
        view_make_path(view_path, sizeof(view_path));

        view_pub = vwm_viewfile_writer_create(view_path);
        if(view_pub == NULL) return -1;

        vk_screen_refresh(vwm->screen);

        if(vwm_viewfile_writer_update(view_pub, win) < 0)
        {
            view_stop_publishing();
            return -1;
        }

        view_last_ms = view_now_ms();
    }

    view_fds[slot] = fd;
    view_count++;

    /* its connection ends the scheduler's sleep when it closes; no
       task owns it, and vwm_view_step looks at it from the step hook */
    vwm_sched_wake_fd_add(sched, fd, NULL);

    if(path != NULL) snprintf(path, path_sz, "%s", view_path);
    getmaxyx(win, i, slot);
    if(rows != NULL) *rows = i;
    if(cols != NULL) *cols = slot;

    return 0;
}

void
vwm_view_step(void)
{
    vwm_t       *vwm;
    WINDOW      *win;
    long long   now;
    int         written;

    if(view_count == 0) return;

    view_check_viewers();
    if(view_count == 0) return;

    /* not yet: the last update was a moment ago */
    now = view_now_ms();
    if(now - view_last_ms < VWM_VIEW_MIN_INTERVAL_MS) return;

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->screen == NULL) return;

    /* the active desktop as composed: what the session's own terminal
       shows, or would if it had one */
    win = vk_screen_get_window(vwm->screen);
    if(win == NULL) return;

    written = vwm_viewfile_writer_update(view_pub, win);
    view_last_ms = now;

    /* only a real change is worth a viewer's attention */
    if(written > 0) view_nudge();
}

int
vwm_view_count(void)
{
    return view_count;
}

void
vwm_view_shutdown(void)
{
    int i;

    for(i = 0; i < VWM_VIEW_MAX; i++)
        view_drop(i);

    /* in case no viewer was left to trigger it */
    view_stop_publishing();
}
