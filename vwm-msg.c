#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
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
        "  list-apps\n"
        "  launch-app <title words...>\n"
        "  send-keys <id> [--type] [--enter] [--file PATH | --text STR | TEXT]\n"
        "            [--key NAME]...\n"
        "      text is pasted (DECSET 2004 if the child armed it) unless --type\n"
        "  capture <id> [--scrollback [N]] [--path FILE]\n"
        "      --scrollback with no N dumps all history plus the live screen\n"
        "  screenshot [--target screen|top] [--path FILE]\n"
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

#define VWM_MSG_MAX_REPLY   (2 * 1024 * 1024)
#define VWM_MSG_MAX_FILE    (200 * 1024)

static int
transact(const char *req)
{
    char                path[PATH_MAX];
    char                *reply;
    size_t              cap = 8192;
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

    reply = malloc(cap);
    if(reply == NULL)
    {
        close(fd);
        fprintf(stderr, "vwm-msg: oom\n");
        return 1;
    }

    for(;;)
    {
        if(off + 1 >= cap)
        {
            char    *nr;
            size_t  ncap;

            if(cap >= VWM_MSG_MAX_REPLY)
            {
                free(reply);
                close(fd);
                fprintf(stderr, "vwm-msg: reply too large\n");
                return 1;
            }

            ncap = cap * 2;
            if(ncap > VWM_MSG_MAX_REPLY)
                ncap = VWM_MSG_MAX_REPLY;
            nr = realloc(reply, ncap);
            if(nr == NULL)
            {
                free(reply);
                close(fd);
                fprintf(stderr, "vwm-msg: oom\n");
                return 1;
            }
            reply = nr;
            cap = ncap;
        }

        n = read(fd, reply + off, cap - 1 - off);
        if(n < 0)
        {
            if(errno == EINTR) continue;
            fprintf(stderr, "vwm-msg: read: %s\n", strerror(errno));
            free(reply);
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
        free(reply);
        return 1;
    }

    {
        char *nl = strchr(reply, '\n');
        if(nl != NULL) *nl = '\0';
    }

    printf("%s\n", reply);

    {
        int ok = (strstr(reply, "\"ok\":true") != NULL ||
            strstr(reply, "\"ok\": true") != NULL);

        free(reply);
        return ok ? 0 : 1;
    }
}

static char *
json_escape_alloc(const char *src, size_t src_len)
{
    size_t  cap = src_len * 6 + 3;
    char    *dst;
    size_t  o = 0;
    size_t  i;

    dst = malloc(cap);
    if(dst == NULL) return NULL;

    dst[o++] = '"';
    for(i = 0; i < src_len; i++)
    {
        unsigned char   c = (unsigned char)src[i];
        const char      *esc = NULL;
        char            tmp[8];

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

            memcpy(dst + o, esc, el);
            o += el;
        }
        else
            dst[o++] = (char)c;
    }

    dst[o++] = '"';
    dst[o] = '\0';
    return dst;
}

static char *
read_file_all(const char *path, size_t *out_len)
{
    FILE        *fp;
    struct stat st;
    char        *buf;
    size_t      n;

    fp = fopen(path, "rb");
    if(fp == NULL)
    {
        fprintf(stderr, "vwm-msg: %s: %s\n", path, strerror(errno));
        return NULL;
    }

    if(fstat(fileno(fp), &st) != 0)
    {
        fprintf(stderr, "vwm-msg: %s: %s\n", path, strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(st.st_size > VWM_MSG_MAX_FILE)
    {
        fprintf(stderr, "vwm-msg: %s: file too large (max %d)\n",
            path, VWM_MSG_MAX_FILE);
        fclose(fp);
        return NULL;
    }

    buf = malloc((size_t)st.st_size + 1);
    if(buf == NULL)
    {
        fclose(fp);
        fprintf(stderr, "vwm-msg: oom\n");
        return NULL;
    }

    n = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    buf[n] = '\0';
    if(out_len != NULL) *out_len = n;
    return buf;
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
        strcmp(op, "list-apps") == 0 ||
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
        char    title[1024];
        char    esc[1024];
        size_t  used = 0;
        int     i;

        if(need(argc, 2, "title") != 0) return 1;

        title[0] = '\0';
        for(i = 2; i < argc; i++)
        {
            size_t  n = strlen(argv[i]);

            if(used > 0)
            {
                if(used + 1 >= sizeof(title))
                {
                    fprintf(stderr, "vwm-msg: title too long\n");
                    return 1;
                }
                title[used++] = ' ';
                title[used] = '\0';
            }
            if(used + n >= sizeof(title))
            {
                fprintf(stderr, "vwm-msg: title too long\n");
                return 1;
            }
            memcpy(title + used, argv[i], n + 1);
            used += n;
        }

        if((title[0] == '"' || title[0] == '\'') &&
            used >= 2 && title[used - 1] == title[0])
        {
            memmove(title, title + 1, used - 2);
            title[used - 2] = '\0';
        }

        if(json_escape(esc, sizeof(esc), title) != 0)
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

    if(strcmp(op, "send-keys") == 0)
    {
        const char  *id = NULL;
        const char  *text = NULL;
        char        *file_text = NULL;
        size_t      file_len = 0;
        int         do_type = 0;
        int         do_enter = 0;
        char        keys_json[2048];
        char        *esc = NULL;
        char        *dyn = NULL;
        size_t      req_sz;
        int         i;
        int         rc;

        keys_json[0] = '\0';

        if(need(argc, 2, "id") != 0) return 1;
        id = argv[2];

        for(i = 3; i < argc; i++)
        {
            if(strcmp(argv[i], "--type") == 0)
            {
                do_type = 1;
                continue;
            }
            if(strcmp(argv[i], "--enter") == 0)
            {
                do_enter = 1;
                continue;
            }
            if(strcmp(argv[i], "--paste") == 0)
            {
                do_type = 0;
                continue;
            }
            if(strcmp(argv[i], "--file") == 0)
            {
                if(need(argc, i + 1, "file") != 0) return 1;
                if(file_text != NULL || text != NULL)
                {
                    fprintf(stderr, "vwm-msg: text already set\n");
                    return 1;
                }
                file_text = read_file_all(argv[++i], &file_len);
                if(file_text == NULL) return 1;
                continue;
            }
            if(strcmp(argv[i], "--text") == 0)
            {
                if(need(argc, i + 1, "text") != 0)
                {
                    free(file_text);
                    return 1;
                }
                if(file_text != NULL || text != NULL)
                {
                    fprintf(stderr, "vwm-msg: text already set\n");
                    free(file_text);
                    return 1;
                }
                text = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--key") == 0)
            {
                char    esc_key[128];

                if(need(argc, i + 1, "key") != 0)
                {
                    free(file_text);
                    return 1;
                }
                if(json_escape(esc_key, sizeof(esc_key), argv[++i]) != 0)
                {
                    fprintf(stderr, "vwm-msg: key too long\n");
                    free(file_text);
                    return 1;
                }
                if(keys_json[0] != '\0')
                    strncat(keys_json, ",", sizeof(keys_json) - 1);
                strncat(keys_json, esc_key, sizeof(keys_json) - 1);
                continue;
            }
            if(strcmp(argv[i], "--") == 0)
            {
                if(i + 1 < argc)
                {
                    if(file_text != NULL || text != NULL)
                    {
                        fprintf(stderr, "vwm-msg: text already set\n");
                        free(file_text);
                        return 1;
                    }
                    text = argv[++i];
                }
                continue;
            }
            if(argv[i][0] == '-')
            {
                fprintf(stderr, "vwm-msg: unknown flag %s\n", argv[i]);
                free(file_text);
                return 1;
            }
            if(file_text != NULL || text != NULL)
            {
                fprintf(stderr, "vwm-msg: text already set\n");
                free(file_text);
                return 1;
            }
            text = argv[i];
        }

        if(file_text == NULL && text == NULL && keys_json[0] == '\0' &&
            !do_enter)
        {
            fprintf(stderr, "vwm-msg: send-keys needs text, --key, or --enter\n");
            return 1;
        }

        if(file_text != NULL)
            esc = json_escape_alloc(file_text, file_len);
        else if(text != NULL)
            esc = json_escape_alloc(text, strlen(text));

        if((file_text != NULL || text != NULL) && esc == NULL)
        {
            free(file_text);
            fprintf(stderr, "vwm-msg: oom\n");
            return 1;
        }

        req_sz = 160 + (esc != NULL ? strlen(esc) : 0) + strlen(keys_json);
        dyn = malloc(req_sz);
        if(dyn == NULL)
        {
            free(esc);
            free(file_text);
            fprintf(stderr, "vwm-msg: oom\n");
            return 1;
        }

        {
            size_t  used;
            int     n;

            n = snprintf(dyn, req_sz, "{\"op\":\"send-keys\",\"id\":%s", id);
            if(n < 0 || (size_t)n >= req_sz)
            {
                free(dyn);
                free(esc);
                free(file_text);
                fprintf(stderr, "vwm-msg: request too long\n");
                return 1;
            }
            used = (size_t)n;

            if(esc != NULL)
            {
                n = snprintf(dyn + used, req_sz - used, ",\"text\":%s", esc);
                if(n < 0 || used + (size_t)n >= req_sz)
                {
                    free(dyn);
                    free(esc);
                    free(file_text);
                    fprintf(stderr, "vwm-msg: request too long\n");
                    return 1;
                }
                used += (size_t)n;
            }
            if(keys_json[0] != '\0')
            {
                n = snprintf(dyn + used, req_sz - used,
                    ",\"keys\":[%s]", keys_json);
                if(n < 0 || used + (size_t)n >= req_sz)
                {
                    free(dyn);
                    free(esc);
                    free(file_text);
                    fprintf(stderr, "vwm-msg: request too long\n");
                    return 1;
                }
                used += (size_t)n;
            }
            if(do_type)
            {
                n = snprintf(dyn + used, req_sz - used, ",\"type\":true");
                if(n < 0 || used + (size_t)n >= req_sz)
                {
                    free(dyn);
                    free(esc);
                    free(file_text);
                    fprintf(stderr, "vwm-msg: request too long\n");
                    return 1;
                }
                used += (size_t)n;
            }
            if(do_enter)
            {
                n = snprintf(dyn + used, req_sz - used, ",\"enter\":true");
                if(n < 0 || used + (size_t)n >= req_sz)
                {
                    free(dyn);
                    free(esc);
                    free(file_text);
                    fprintf(stderr, "vwm-msg: request too long\n");
                    return 1;
                }
                used += (size_t)n;
            }
            if(used + 2 > req_sz)
            {
                free(dyn);
                free(esc);
                free(file_text);
                fprintf(stderr, "vwm-msg: request too long\n");
                return 1;
            }
            dyn[used++] = '}';
            dyn[used] = '\0';
        }

        rc = transact(dyn);
        free(dyn);
        free(esc);
        free(file_text);
        return rc;
    }

    if(strcmp(op, "capture") == 0)
    {
        const char  *id;
        const char  *path = NULL;
        int         have_sb = 0;
        const char  *sb = NULL;
        char        extra[PATH_MAX + 128];
        int         i;

        if(need(argc, 2, "id") != 0) return 1;
        id = argv[2];
        extra[0] = '\0';

        for(i = 3; i < argc; i++)
        {
            if(strcmp(argv[i], "--scrollback") == 0)
            {
                have_sb = 1;
                if(i + 1 < argc && argv[i + 1][0] != '-')
                    sb = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--path") == 0)
            {
                if(need(argc, i + 1, "path") != 0) return 1;
                path = argv[++i];
                continue;
            }
            fprintf(stderr, "vwm-msg: unknown flag %s\n", argv[i]);
            return 1;
        }

        if(have_sb)
        {
            if(sb != NULL)
                snprintf(extra, sizeof(extra), ",\"scrollback\":%s", sb);
            else
                snprintf(extra, sizeof(extra), ",\"scrollback\":true");
        }

        if(path != NULL)
        {
            char    esc[PATH_MAX + 8];
            char    piece[PATH_MAX + 32];

            if(json_escape(esc, sizeof(esc), path) != 0)
                return 1;
            snprintf(piece, sizeof(piece), ",\"path\":%s", esc);
            if(strlen(extra) + strlen(piece) >= sizeof(extra))
                return 1;
            strcat(extra, piece);
        }

        snprintf(req, sizeof(req),
            "{\"op\":\"capture\",\"id\":%s%s}", id, extra);
        return transact(req);
    }

    if(strcmp(op, "screenshot") == 0)
    {
        const char  *target = "screen";
        const char  *path = NULL;
        char        extra[PATH_MAX + 128];
        int         i;

        extra[0] = '\0';

        for(i = 2; i < argc; i++)
        {
            if(strcmp(argv[i], "--target") == 0)
            {
                if(need(argc, i + 1, "target") != 0) return 1;
                target = argv[++i];
                continue;
            }
            if(strcmp(argv[i], "--path") == 0)
            {
                if(need(argc, i + 1, "path") != 0) return 1;
                path = argv[++i];
                continue;
            }
            fprintf(stderr, "vwm-msg: unknown flag %s\n", argv[i]);
            return 1;
        }

        {
            char    esc[64];

            if(json_escape(esc, sizeof(esc), target) != 0)
                return 1;
            snprintf(extra, sizeof(extra), ",\"target\":%s", esc);
        }

        if(path != NULL)
        {
            char    esc[PATH_MAX + 8];
            char    piece[PATH_MAX + 32];

            if(json_escape(esc, sizeof(esc), path) != 0)
                return 1;
            snprintf(piece, sizeof(piece), ",\"path\":%s", esc);
            if(strlen(extra) + strlen(piece) >= sizeof(extra))
                return 1;
            strcat(extra, piece);
        }

        snprintf(req, sizeof(req), "{\"op\":\"screenshot\"%s}", extra);
        return transact(req);
    }

    fprintf(stderr, "vwm-msg: unknown op '%s'\n", op);
    usage(stderr);
    return 1;
}
