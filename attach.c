#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "attach.h"
#include "session.h"

/* the longest line the session sends this client */
#define VWM_ATTACH_LINE     512

/* how long to wait for the terminal to answer that it has caught up,
   in milliseconds.  A terminal that never answers costs this much at
   exit and no more. */
#define VWM_ATTACH_DRAIN_MS 400

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

/*
    Throw away what the terminal sent while the session was leaving it.

    The session asks the terminal to report the mouse, and tells it to
    stop as it leaves.  A terminal reads that request when it gets to
    it: a report for a pointer that moved in the meantime is already on
    its way, or is sent a moment later, and would land in the shell as
    typed text ("35;17;18M").

    The terminal is asked a question it answers in turn (a status
    report, ESC [ 5 n, answered ESC [ 0 n).  By the time the answer
    arrives it has acted on everything written before the question,
    the request to stop reporting included, so whatever comes in up to
    and including the answer is stale and is discarded.  A terminal
    that does not answer is given VWM_ATTACH_DRAIN_MS.

    `saved` is how the terminal is set up for the shell; it is put back
    by the caller afterwards.
*/
static void
attach_drain_input(int tty_fd, const struct termios *saved)
{
    static const char   ask[] = "\033[5n";
    static const char   answer[] = "\033[0n";
    struct termios      quiet;
    struct timespec     start;
    struct timespec     now;
    struct pollfd       pfd;
    char                buf[256];
    size_t              matched = 0;
    long                elapsed_ms;
    ssize_t             got;
    ssize_t             i;
    int                 ready;

    if(tty_fd < 0 || saved == NULL) return;

    /* no echo and no line editing while this goes on, or the tty
       would print the very bytes being thrown away.  TCSAFLUSH drops
       what is already waiting. */
    quiet = *saved;
    cfmakeraw(&quiet);
    quiet.c_cc[VMIN] = 0;
    quiet.c_cc[VTIME] = 0;
    if(tcsetattr(tty_fd, TCSAFLUSH, &quiet) != 0) return;

    if(write(tty_fd, ask, sizeof(ask) - 1) != (ssize_t)(sizeof(ask) - 1))
        return;

    clock_gettime(CLOCK_MONOTONIC, &start);

    /* read and discard until the answer has gone by, or time is up */
    for(;;)
    {
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
            + (now.tv_nsec - start.tv_nsec) / 1000000L;
        if(elapsed_ms >= VWM_ATTACH_DRAIN_MS) break;

        pfd.fd = tty_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        ready = poll(&pfd, 1, (int)(VWM_ATTACH_DRAIN_MS - elapsed_ms));
        if(ready < 0 && errno == EINTR) continue;
        if(ready <= 0) break;

        got = read(tty_fd, buf, sizeof(buf));
        if(got <= 0) break;

        /* look for the answer, which may arrive split across reads */
        for(i = 0; i < got; i++)
        {
            if(buf[i] == answer[matched])
                matched++;
            else
                matched = (buf[i] == answer[0]) ? 1 : 0;

            if(matched == sizeof(answer) - 1) return;
        }
    }
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
    char            hint[VWM_SESSION_SID_MAX + 16];
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

    /* mouse reports the terminal sent while the session was leaving it
       must not reach the shell */
    if(have_saved) attach_drain_input(tty_fd, &saved);

    /* vwm restores the modes itself when it leaves in good order; doing
       it again costs nothing and covers the times it could not.
       Anything still unread is dropped with it. */
    if(have_saved) tcsetattr(tty_fd, TCSAFLUSH, &saved);
    if(tty_fd >= 0) close(tty_fd);

    /* the commands as they have to be typed for this session */
    vwm_session_hint(hint, sizeof(hint));

    if(strcmp(word, "detached") == 0)
    {
        printf("vwm: detached.  The session is still running:"
            " vwm-resume%s returns to it, vwm-stop%s ends it.\n",
            hint, hint);
        return 0;
    }

    if(strcmp(word, "moved") == 0)
    {
        printf("vwm: the session moved to another terminal."
            "  vwm-resume%s brings it back here.\n", hint);
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
