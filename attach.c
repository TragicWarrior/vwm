#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "attach.h"

/* the longest line the session sends this client */
#define VWM_ATTACH_LINE     512

/*
    Connect to the session's control socket.  Returns the descriptor, or
    -1 with the reason printed.
*/
static int
attach_connect(const char *sock)
{
    struct sockaddr_un  addr;
    int                 fd;

    if(sock == NULL || strlen(sock) >= sizeof(addr.sun_path))
    {
        fprintf(stderr, "vwm: control socket path too long\n");
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0)
    {
        fprintf(stderr, "vwm: socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, sock, strlen(sock) + 1);

    if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        fprintf(stderr, "vwm: no session answers on %s: %s\n", sock,
            strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/*
    Append `src` to the request as a JSON string.  A terminal path or
    type holds no control characters; one that does is refused rather
    than escaped.  Returns 0, or -1 if it does not fit or is not clean.
*/
static int
attach_put_string(char *dst, size_t dst_sz, const char *src)
{
    size_t  o = strlen(dst);
    size_t  i;

    if(o + 2 >= dst_sz) return -1;
    dst[o++] = '"';

    for(i = 0; src[i] != '\0'; i++)
    {
        unsigned char   c = (unsigned char)src[i];

        if(c < 0x20) return -1;
        if(o + 4 >= dst_sz) return -1;

        if(c == '"' || c == '\\') dst[o++] = '\\';
        dst[o++] = (char)c;
    }

    dst[o++] = '"';
    dst[o] = '\0';

    return 0;
}

/*
    Read one line from the session, blocking for as long as it takes --
    the second line this client reads arrives only when the session
    leaves the terminal.  Returns 0 with the line in `buf` (no newline),
    or -1 when the connection ended first.
*/
static int
attach_read_line(int fd, char *buf, size_t n)
{
    size_t  used = 0;

    while(used < n - 1)
    {
        char    c;
        ssize_t got = read(fd, &c, 1);

        if(got < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        if(got == 0) return -1;

        if(c == '\n')
        {
            buf[used] = '\0';
            return 0;
        }

        buf[used++] = c;
    }

    buf[used] = '\0';

    return 0;
}

/*
    Pull the string value of `key` out of a one-line JSON object.  Good
    for the short fixed replies the session sends here, which is all it
    is used on.  Returns `out`, empty when the key is absent.
*/
static const char *
attach_json_str(const char *line, const char *key, char *out, size_t n)
{
    char        pat[48];
    const char  *p;
    size_t      o = 0;

    out[0] = '\0';

    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(line, pat);
    if(p == NULL) return out;

    for(p += strlen(pat); *p != '\0' && *p != '"' && o < n - 1; p++)
        out[o++] = *p;
    out[o] = '\0';

    return out;
}

/* see attach.h */
int
vwm_attach_run(const char *sock, const char *tty, const char *term)
{
    struct termios  saved;
    bool            have_saved = false;
    char            req[VWM_ATTACH_LINE];
    char            line[VWM_ATTACH_LINE];
    char            word[128];
    int             tty_fd;
    int             fd;

    if(tty == NULL || tty[0] == '\0')
    {
        fprintf(stderr, "vwm: not on a terminal\n");
        return 1;
    }

    fd = attach_connect(sock);
    if(fd < 0) return 1;

    /* remember how the terminal is set up, to put it back whatever
       becomes of the session -- including one that dies without
       cleaning up after itself */
    tty_fd = open(tty, O_RDWR | O_NOCTTY);
    if(tty_fd >= 0 && tcgetattr(tty_fd, &saved) == 0) have_saved = true;

    /* the request: {"op":"attach","tty":"...","term":"..."} */
    snprintf(req, sizeof(req), "{\"op\":\"attach\",\"tty\":");
    if(attach_put_string(req, sizeof(req), tty) != 0)
    {
        fprintf(stderr, "vwm: bad terminal path\n");
        close(fd);
        if(tty_fd >= 0) close(tty_fd);
        return 1;
    }
    if(term != NULL && term[0] != '\0')
    {
        strncat(req, ",\"term\":", sizeof(req) - strlen(req) - 1);
        if(attach_put_string(req, sizeof(req), term) != 0)
        {
            fprintf(stderr, "vwm: bad terminal type\n");
            close(fd);
            if(tty_fd >= 0) close(tty_fd);
            return 1;
        }
    }
    strncat(req, "}\n", sizeof(req) - strlen(req) - 1);

    if(write(fd, req, strlen(req)) != (ssize_t)strlen(req))
    {
        fprintf(stderr, "vwm: write: %s\n", strerror(errno));
        close(fd);
        if(tty_fd >= 0) close(tty_fd);
        return 1;
    }

    /* first line: did the session accept this terminal? */
    if(attach_read_line(fd, line, sizeof(line)) != 0
        || strstr(line, "\"ok\":true") == NULL)
    {
        attach_json_str(line, "error", word, sizeof(word));

        if(strcmp(word, "inside this session") == 0)
            fprintf(stderr, "vwm: this terminal is inside the vwm session."
                "  Run this from a terminal outside vwm.\n");
        else
            fprintf(stderr, "vwm: the session did not accept this "
                "terminal%s%s\n", (word[0] != '\0') ? ": " : "", word);

        close(fd);
        if(tty_fd >= 0) close(tty_fd);
        return 1;
    }

    /* vwm has the terminal now and puts it in raw mode, where these
       keys are ordinary input.  Ignore the signals all the same: one
       typed in the moment before raw mode takes hold should not end or
       stop this program and leave the shell and vwm reading the same
       keyboard. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    /* second line: the session has left this terminal, and says why.
       This is where the program spends its life. */
    if(attach_read_line(fd, line, sizeof(line)) != 0)
        snprintf(word, sizeof(word), "lost");
    else
        attach_json_str(line, "reason", word, sizeof(word));

    close(fd);

    /* a session that went without a word left the terminal as it was
       using it: leave the alternate screen, show the cursor and stop
       mouse reporting on its behalf */
    if(strcmp(word, "lost") == 0 && tty_fd >= 0)
    {
        static const char   reset[] =
            "\033[?1000l\033[?1002l\033[?1003l\033[?1006l"
            "\033[?1049l\033[?25h\033[0m\r\n";

        if(write(tty_fd, reset, sizeof(reset) - 1) < 0)
        {
            /* nothing to be done about a terminal that will not take it */
        }
    }

    /* vwm restores the modes itself when it leaves in good order; doing
       it again costs nothing and covers the times it could not */
    if(have_saved) tcsetattr(tty_fd, TCSANOW, &saved);
    if(tty_fd >= 0) close(tty_fd);

    if(strcmp(word, "detached") == 0)
    {
        printf("vwm: detached.  The session is still running:"
            " vwm-resume returns to it, vwm-stop ends it.\n");
        return 0;
    }

    if(strcmp(word, "moved") == 0)
    {
        printf("vwm: the session moved to another terminal."
            "  vwm-resume brings it back here.\n");
        return 0;
    }

    if(strcmp(word, "stopped") == 0)
    {
        printf("vwm: session ended.\n");
        return 0;
    }

    if(strcmp(word, "failed") == 0)
    {
        fprintf(stderr, "vwm: the session could not use this terminal.\n");
        return 1;
    }

    fprintf(stderr, "vwm: lost contact with the session.\n");

    return 1;
}
