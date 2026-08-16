#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>

static void
usage(FILE *fp)
{
    fprintf(fp,
        "Usage: vwm-msg <op> [args]\n"
        "\n"
        "  ping\n"
        "  list-windows\n"
        "  list-desktops\n"
        "  focused\n"
        "  desktop <n>\n"
        "  focus <id>\n"
        "  close <id>\n"
        "  minimize <id>\n"
        "  restore <id>\n"
        "  move <id> [--dx N] [--dy N] [--x N] [--y N]\n"
        "  resize <id> [--dw N] [--dh N] [--width N] [--height N]\n"
        "  launch --bin PATH [--profile NAME] [--width N] [--height N]\n"
        "         [--scrollback N] [--start-home] [-- ARG...]\n"
        "  launch-app <title>\n"
        "\n"
        "Talks to VWM_CONTROL_SOCK, else ~/.config/vwm/control.sock.\n");
}

static void
sock_path(char *buf, size_t n)
{
    const char  *env;
    const char  *home;

    env = getenv("VWM_CONTROL_SOCK");
    if(env != NULL && env[0] != '\0')
    {
        snprintf(buf, n, "%s", env);
        return;
    }

    home = getenv("HOME");
    if(home != NULL && home[0] != '\0')
        snprintf(buf, n, "%s/.config/vwm/control.sock", home);
    else
        snprintf(buf, n, "/tmp/vwm-%d.ctl", (int)getuid());
}

static int
json_escape(char *dst, size_t dst_sz, const char *src)
{
    size_t  o = 0;
    size_t  i;

    if(dst_sz < 3) return -1;
    dst[o++] = '"';

    for(i = 0; src[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)src[i];
        const char    *esc = NULL;
        char          tmp[8];

        if(c == '"' || c == '\\')
        {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            tmp[2] = '\0';
            esc = tmp;
        }
        else if(c == '\n')
            esc = "\\n";
        else if(c == '\r')
            esc = "\\r";
        else if(c == '\t')
            esc = "\\t";
        else if(c < 0x20)
        {
            snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            esc = tmp;
        }

        if(esc != NULL)
        {
            size_t el = strlen(esc);
            if(o + el + 2 >= dst_sz) return -1;
            memcpy(dst + o, esc, el);
            o += el;
        }
        else
        {
            if(o + 2 >= dst_sz) return -1;
            dst[o++] = (char)c;
        }
    }

    dst[o++] = '"';
    dst[o] = '\0';
    return 0;
}

static int
transact(const char *req)
{
    char                path[PATH_MAX];
    char                reply[8192];
    struct sockaddr_un  addr;
    int                 fd;
    size_t              off = 0;
    ssize_t             n;

    sock_path(path, sizeof(path));

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0)
    {
        fprintf(stderr, "vwm-msg: socket: %s\n", strerror(errno));
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(strlen(path) >= sizeof(addr.sun_path))
    {
        fprintf(stderr, "vwm-msg: socket path too long\n");
        close(fd);
        return 1;
    }
    memcpy(addr.sun_path, path, strlen(path) + 1);

    if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        fprintf(stderr, "vwm-msg: connect %s: %s\n", path, strerror(errno));
        close(fd);
        return 1;
    }

    {
        size_t  len = strlen(req);
        char    nl = '\n';

        if(write(fd, req, len) != (ssize_t)len ||
            write(fd, &nl, 1) != 1)
        {
            fprintf(stderr, "vwm-msg: write: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }

    while(off < sizeof(reply) - 1)
    {
        n = read(fd, reply + off, sizeof(reply) - 1 - off);
        if(n < 0)
        {
            if(errno == EINTR) continue;
            fprintf(stderr, "vwm-msg: read: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        if(n == 0) break;
        off += (size_t)n;
        reply[off] = '\0';
        if(memchr(reply, '\n', off) != NULL) break;
    }

    close(fd);

    if(off == 0)
    {
        fprintf(stderr, "vwm-msg: empty reply\n");
        return 1;
    }

    {
        char *nl = strchr(reply, '\n');
        if(nl != NULL) *nl = '\0';
    }

    printf("%s\n", reply);

    if(strstr(reply, "\"ok\":true") != NULL ||
        strstr(reply, "\"ok\": true") != NULL)
        return 0;

    return 1;
}

static int
need(int argc, int i, const char *what)
{
    if(i >= argc)
    {
        fprintf(stderr, "vwm-msg: missing %s\n", what);
        return -1;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    char    req[8192];
    const char *op;

    if(argc < 2)
    {
        usage(stderr);
        return 1;
    }

    op = argv[1];

    if(strcmp(op, "--help") == 0 || strcmp(op, "-h") == 0)
    {
        usage(stdout);
        return 0;
    }

    if(strcmp(op, "ping") == 0 ||
        strcmp(op, "list-windows") == 0 ||
        strcmp(op, "list-desktops") == 0 ||
        strcmp(op, "focused") == 0)
    {
        snprintf(req, sizeof(req), "{\"op\":\"%s\"}", op);
        return transact(req);
    }

    if(strcmp(op, "desktop") == 0)
    {
        if(need(argc, 2, "desktop number") != 0) return 1;
        snprintf(req, sizeof(req), "{\"op\":\"desktop\",\"desktop\":%s}",
            argv[2]);
        return transact(req);
    }

    if(strcmp(op, "focus") == 0 || strcmp(op, "close") == 0 ||
        strcmp(op, "minimize") == 0 || strcmp(op, "restore") == 0)
    {
        if(need(argc, 2, "id") != 0) return 1;
        snprintf(req, sizeof(req), "{\"op\":\"%s\",\"id\":%s}", op, argv[2]);
        return transact(req);
    }

    if(strcmp(op, "launch-app") == 0)
    {
        char    esc[1024];

        if(need(argc, 2, "title") != 0) return 1;
        if(json_escape(esc, sizeof(esc), argv[2]) != 0)
        {
            fprintf(stderr, "vwm-msg: title too long\n");
            return 1;
        }
        snprintf(req, sizeof(req), "{\"op\":\"launch-app\",\"title\":%s}",
            esc);
        return transact(req);
    }

    if(strcmp(op, "move") == 0 || strcmp(op, "resize") == 0)
    {
        char    extra[512];
        int     i;

        if(need(argc, 2, "id") != 0) return 1;
        extra[0] = '\0';

        for(i = 3; i < argc; i++)
        {
            const char  *key = NULL;

            if(strcmp(argv[i], "--dx") == 0) key = "dx";
            else if(strcmp(argv[i], "--dy") == 0) key = "dy";
            else if(strcmp(argv[i], "--x") == 0) key = "x";
            else if(strcmp(argv[i], "--y") == 0) key = "y";
            else if(strcmp(argv[i], "--dw") == 0) key = "dw";
            else if(strcmp(argv[i], "--dh") == 0) key = "dh";
            else if(strcmp(argv[i], "--width") == 0) key = "width";
            else if(strcmp(argv[i], "--height") == 0) key = "height";
            else
            {
                fprintf(stderr, "vwm-msg: unknown flag %s\n", argv[i]);
                return 1;
            }

            if(need(argc, i + 1, key) != 0) return 1;
            i++;
            {
                char    piece[64];
                snprintf(piece, sizeof(piece), ",\"%s\":%s", key, argv[i]);
                if(strlen(extra) + strlen(piece) >= sizeof(extra))
                    return 1;
                strcat(extra, piece);
            }
        }

        snprintf(req, sizeof(req),
            "{\"op\":\"%s\",\"id\":%s%s}", op, argv[2], extra);
        return transact(req);
    }

    if(strcmp(op, "launch") == 0)
    {
        const char  *bin = NULL;
        const char  *profile = NULL;
        const char  *width = NULL;
        const char  *height = NULL;
        const char  *scrollback = NULL;
        int         start_home = 0;
        int         dash = 0;
        int         i;
        char        esc_bin[PATH_MAX + 8];
        char        esc_prof[256];
        char        args_json[2048];

        args_json[0] = '\0';

        for(i = 2; i < argc; i++)
        {
            if(dash)
            {
                char    esc[512];
                if(json_escape(esc, sizeof(esc), argv[i]) != 0) return 1;
                if(args_json[0] != '\0')
                    strncat(args_json, ",", sizeof(args_json) - 1);
                strncat(args_json, esc, sizeof(args_json) - 1);
                continue;
            }

            if(strcmp(argv[i], "--") == 0)
            {
                dash = 1;
                continue;
            }
            if(strcmp(argv[i], "--bin") == 0)
            {
                if(need(argc, i + 1, "bin") != 0) return 1;
                bin = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--profile") == 0)
            {
                if(need(argc, i + 1, "profile") != 0) return 1;
                profile = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--width") == 0)
            {
                if(need(argc, i + 1, "width") != 0) return 1;
                width = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--height") == 0)
            {
                if(need(argc, i + 1, "height") != 0) return 1;
                height = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--scrollback") == 0)
            {
                if(need(argc, i + 1, "scrollback") != 0) return 1;
                scrollback = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--start-home") == 0)
            {
                start_home = 1;
                continue;
            }

            fprintf(stderr, "vwm-msg: unknown flag %s\n", argv[i]);
            return 1;
        }

        if(bin == NULL)
        {
            fprintf(stderr, "vwm-msg: launch requires --bin\n");
            return 1;
        }

        if(json_escape(esc_bin, sizeof(esc_bin), bin) != 0) return 1;

        if(snprintf(req, sizeof(req), "{\"op\":\"launch\",\"bin\":%s",
            esc_bin) >= (int)sizeof(req))
        {
            fprintf(stderr, "vwm-msg: request too long\n");
            return 1;
        }

        if(profile != NULL)
        {
            if(json_escape(esc_prof, sizeof(esc_prof), profile) != 0)
                return 1;
            strncat(req, ",\"profile\":", sizeof(req) - 1);
            strncat(req, esc_prof, sizeof(req) - 1);
        }
        if(width != NULL)
        {
            strncat(req, ",\"width\":", sizeof(req) - 1);
            strncat(req, width, sizeof(req) - 1);
        }
        if(height != NULL)
        {
            strncat(req, ",\"height\":", sizeof(req) - 1);
            strncat(req, height, sizeof(req) - 1);
        }
        if(scrollback != NULL)
        {
            strncat(req, ",\"scrollback\":", sizeof(req) - 1);
            strncat(req, scrollback, sizeof(req) - 1);
        }
        if(start_home)
            strncat(req, ",\"start_home\":1", sizeof(req) - 1);
        if(args_json[0] != '\0')
        {
            strncat(req, ",\"args\":[", sizeof(req) - 1);
            strncat(req, args_json, sizeof(req) - 1);
            strncat(req, "]", sizeof(req) - 1);
        }
        strncat(req, "}", sizeof(req) - 1);

        return transact(req);
    }

    fprintf(stderr, "vwm-msg: unknown op '%s'\n", op);
    usage(stderr);
    return 1;
}
