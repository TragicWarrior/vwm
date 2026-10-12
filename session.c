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
#include <limits.h>

#include "session.h"

void
vwm_session_hint(char *buf, size_t n)
{
    const char  *path;
    const char  *base;
    const char  *dot;
    char        sid[VWM_SESSION_SID_MAX + 1];
    size_t      len;

    if(buf == NULL || n == 0) return;
    buf[0] = '\0';

    path = getenv("VWM_CONTROL_SOCK");
    if(path == NULL || path[0] == '\0') return;

    /* the file name, without the directories */
    base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;

    /* a named session's socket is "control-<id>.sock" */
    if(strncmp(base, "control-", 8) != 0) return;
    base += 8;

    dot = strrchr(base, '.');
    if(dot == NULL || strcmp(dot, ".sock") != 0) return;

    len = (size_t)(dot - base);
    if(len < 1 || len > VWM_SESSION_SID_MAX) return;

    memcpy(sid, base, len);
    sid[len] = '\0';

    /* anything else between the two was not put there by us */
    if(!vwm_session_sid_valid(sid)) return;

    snprintf(buf, n, " --sid %s", sid);
}

int
vwm_session_sid_valid(const char *sid)
{
    size_t  len;
    size_t  i;

    if(sid == NULL) return 0;

    len = strlen(sid);
    if(len < 1 || len > VWM_SESSION_SID_MAX) return 0;

    for(i = 0; i < len; i++)
    {
        char c = sid[i];

        if(c >= 'a' && c <= 'z') continue;
        if(c >= 'A' && c <= 'Z') continue;
        if(c >= '0' && c <= '9') continue;
        if(c == '-' || c == '_') continue;

        return 0;
    }

    return 1;
}

int
vwm_session_select(const char *sid)
{
    const char  *home;
    char        path[PATH_MAX];
    int         n;

    if(!vwm_session_sid_valid(sid)) return -1;

    /* a named session's socket lives beside the default one, which is
       in the home directory */
    home = getenv("HOME");
    if(home == NULL || home[0] == '\0') return -1;

    /* the default session keeps the path it has always had.  it is set
       outright rather than left to the default, so that asking for it
       by id from inside another session really does leave that one */
    if(strcmp(sid, VWM_SESSION_DEFAULT) == 0)
        n = snprintf(path, sizeof(path), "%s/.config/vwm/control.sock", home);
    else
        n = snprintf(path, sizeof(path), "%s/.config/vwm/control-%s.sock",
            home, sid);

    if(n < 0 || (size_t)n >= sizeof(path)) return -1;

    return setenv("VWM_CONTROL_SOCK", path, 1);
}

int
vwm_session_args(int *argc, char **argv, int scan, const char *prog)
{
    const char  *sid = NULL;
    int         take = 0;       /* how many arguments the option used */
    int         last;
    int         i, j;

    if(argc == NULL || argv == NULL) return 0;

    /* only this far: beyond it the arguments belong to something else
       (text for vwm-msg to send, say) and are not ours to interpret */
    last = (scan < *argc - 1) ? scan : *argc - 1;

    for(i = 1; i <= last; i++)
    {
        /* "--sid ID" */
        if(strcmp(argv[i], "--sid") == 0)
        {
            if(i + 1 >= *argc)
            {
                fprintf(stderr, "%s: --sid needs a session id\n", prog);
                return -1;
            }

            sid = argv[i + 1];
            take = 2;
            break;
        }

        /* "--sid=ID" */
        if(strncmp(argv[i], "--sid=", 6) == 0)
        {
            sid = argv[i] + 6;
            take = 1;
            break;
        }
    }

    if(sid == NULL) return 0;

    if(!vwm_session_sid_valid(sid))
    {
        fprintf(stderr,
            "%s: '%s' is not a session id (letters, digits, '-' and '_', "
            "at most %d)\n", prog, sid, VWM_SESSION_SID_MAX);
        return -1;
    }

    if(vwm_session_select(sid) != 0)
    {
        fprintf(stderr, "%s: cannot select session '%s' (is HOME set?)\n",
            prog, sid);
        return -1;
    }

    /* close the gap the option leaves; argv stays NULL-terminated */
    for(j = i; j + take <= *argc; j++)
        argv[j] = argv[j + take];

    *argc -= take;

    return 0;
}
