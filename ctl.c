#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include <vdk.h>

#include "cJSON.h"
#include "vwm.h"
#include "private.h"
#include "modules.h"
#include "winman.h"
#include "strings.h"
#include "ctl.h"

#define VWM_CTL_MAX_REQ     8192

static int      listen_fd = -1;
static char     sock_path[PATH_MAX];

static void     ctl_reply(int fd, int ok, cJSON *data, const char *err);
static void     ctl_dispatch(int fd, cJSON *req);
static vk_widget_t *ctl_find_id(uint32_t id, int *desktop);
static int      ctl_switch_desktop(int n);

static void
ctl_sock_path(char *buf, size_t n)
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
ctl_mkdir_parent(const char *path)
{
    char        dir[PATH_MAX];
    char        *slash;
    struct stat st;

    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if(slash == NULL || slash == dir)
        return 0;

    *slash = '\0';
    if(stat(dir, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;

    /* one level is enough: ~/.config/vwm ; create ~/.config then vwm */
    {
        char    *slash2 = strrchr(dir, '/');

        if(slash2 != NULL && slash2 != dir)
        {
            *slash2 = '\0';
            mkdir(dir, 0700);
            *slash2 = '/';
        }
    }

    if(mkdir(dir, 0700) == 0)
        return 0;

    return (errno == EEXIST) ? 0 : -1;
}

int
vwm_ctl_listen_fd(void)
{
    return listen_fd;
}

int
vwm_ctl_init(void)
{
    struct sockaddr_un  addr;
    int                 fd;
    int                 flags;

    ctl_sock_path(sock_path, sizeof(sock_path));

    if(ctl_mkdir_parent(sock_path) != 0)
        return -1;

    unlink(sock_path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(strlen(sock_path) >= sizeof(addr.sun_path))
    {
        close(fd);
        sock_path[0] = '\0';
        return -1;
    }
    memcpy(addr.sun_path, sock_path, strlen(sock_path) + 1);

    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(fd);
        sock_path[0] = '\0';
        return -1;
    }

    chmod(sock_path, 0600);

    if(listen(fd, 4) != 0)
    {
        unlink(sock_path);
        close(fd);
        sock_path[0] = '\0';
        return -1;
    }

    flags = fcntl(fd, F_GETFL);
    if(flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    listen_fd = fd;
    setenv("VWM_CONTROL_SOCK", sock_path, 1);

    return 0;
}

void
vwm_ctl_shutdown(void)
{
    if(listen_fd >= 0)
    {
        close(listen_fd);
        listen_fd = -1;
    }

    if(sock_path[0] != '\0')
    {
        unlink(sock_path);
        sock_path[0] = '\0';
    }
}

static int
ctl_peer_ok(int fd)
{
#ifdef SO_PEERCRED
    struct ucred    cred;
    socklen_t       len = sizeof(cred);

    if(getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0)
        return 0;

    if(cred.uid != getuid() && cred.uid != geteuid())
        return 0;
#else
    (void)fd;
#endif

    return 1;
}

static void
ctl_serve(int cfd)
{
    char    buf[VWM_CTL_MAX_REQ];
    size_t  used = 0;
    ssize_t n;
    cJSON   *req;

    if(!ctl_peer_ok(cfd))
    {
        ctl_reply(cfd, 0, NULL, "unauthorized");
        close(cfd);
        return;
    }

    while(used < sizeof(buf) - 1)
    {
        n = read(cfd, buf + used, sizeof(buf) - 1 - used);
        if(n < 0)
        {
            if(errno == EINTR) continue;
            close(cfd);
            return;
        }
        if(n == 0)
            break;

        used += (size_t)n;
        buf[used] = '\0';
        if(memchr(buf, '\n', used) != NULL)
            break;
    }

    if(used == 0)
    {
        close(cfd);
        return;
    }

    {
        char *nl = strchr(buf, '\n');
        if(nl != NULL) *nl = '\0';
    }

    req = cJSON_Parse(buf);
    if(req == NULL)
    {
        ctl_reply(cfd, 0, NULL, "invalid json");
        close(cfd);
        return;
    }

    ctl_dispatch(cfd, req);
    cJSON_Delete(req);
    close(cfd);
}

void
vwm_ctl_poll(void)
{
    int cfd;

    if(listen_fd < 0) return;

    for(;;)
    {
        cfd = accept(listen_fd, NULL, NULL);
        if(cfd < 0)
        {
            if(errno == EINTR) continue;
            break;
        }

        ctl_serve(cfd);
    }
}

static void
ctl_reply(int fd, int ok, cJSON *data, const char *err)
{
    cJSON   *root;
    char    *txt;

    root = cJSON_CreateObject();
    if(root == NULL)
    {
        if(data != NULL) cJSON_Delete(data);
        return;
    }

    cJSON_AddBoolToObject(root, "ok", ok ? 1 : 0);
    if(ok && data != NULL)
        cJSON_AddItemToObject(root, "data", data);
    else if(data != NULL)
        cJSON_Delete(data);

    if(!ok && err != NULL)
        cJSON_AddStringToObject(root, "error", err);

    txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(txt == NULL) return;

    {
        size_t  len = strlen(txt);
        char    nl = '\n';

        if(write(fd, txt, len) < 0) { /* ignore */ }
        if(write(fd, &nl, 1) < 0) { /* ignore */ }
    }

    free(txt);
}

static vk_widget_t *
ctl_find_id(uint32_t id, int *desktop)
{
    vwm_t   *vwm = vwm_get_instance();
    int     i, j, n;

    if(id == 0 || vwm == NULL) return NULL;

    for(i = 0; i < vwm->surface_count; i++)
    {
        if(vwm->decks[i] == NULL) continue;

        n = vk_deck_count(vwm->decks[i]);
        for(j = 0; j < n; j++)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->decks[i], j);

            if(w != NULL && vk_widget_get_id(w) == id)
            {
                if(desktop != NULL) *desktop = i;
                return w;
            }
        }
    }

    return NULL;
}

static int
ctl_switch_desktop(int n)
{
    vwm_t   *vwm = vwm_get_instance();

    if(vwm == NULL || n < 0 || n >= vwm->surface_count)
        return -1;

    if(vk_screen_get_active_surface(vwm->screen) != n)
        vk_screen_set_surface(vwm->screen, n);

    vk_screen_refresh(vwm->screen);
    return 0;
}

static uint32_t
ctl_json_u32(cJSON *req, const char *key, int *present)
{
    cJSON   *item;

    if(present != NULL) *present = 0;
    item = cJSON_GetObjectItemCaseSensitive(req, key);
    if(!cJSON_IsNumber(item))
        return 0;

    if(present != NULL) *present = 1;
    return (uint32_t)item->valuedouble;
}

static int
ctl_json_int(cJSON *req, const char *key, int *present)
{
    cJSON   *item;

    if(present != NULL) *present = 0;
    item = cJSON_GetObjectItemCaseSensitive(req, key);
    if(!cJSON_IsNumber(item))
        return 0;

    if(present != NULL) *present = 1;
    return (int)item->valuedouble;
}

static const char *
ctl_json_str(cJSON *req, const char *key)
{
    cJSON   *item;

    item = cJSON_GetObjectItemCaseSensitive(req, key);
    if(!cJSON_IsString(item) || item->valuestring == NULL)
        return NULL;

    return item->valuestring;
}

static void
op_ping(int fd)
{
    cJSON   *data = cJSON_CreateObject();

    if(data != NULL)
        cJSON_AddStringToObject(data, "version", VWM_VERSION);

    ctl_reply(fd, 1, data, NULL);
}

static void
op_list_desktops(int fd)
{
    vwm_t   *vwm = vwm_get_instance();
    cJSON   *data;
    cJSON   *arr;
    int     i;

    if(vwm == NULL)
    {
        ctl_reply(fd, 0, NULL, "no vwm");
        return;
    }

    data = cJSON_CreateObject();
    arr = cJSON_CreateArray();
    if(data == NULL || arr == NULL)
    {
        if(data != NULL) cJSON_Delete(data);
        if(arr != NULL) cJSON_Delete(arr);
        ctl_reply(fd, 0, NULL, "oom");
        return;
    }

    cJSON_AddNumberToObject(data, "count", vwm->surface_count);
    cJSON_AddNumberToObject(data, "current",
        vk_screen_get_active_surface(vwm->screen));

    for(i = 0; i < vwm->surface_count; i++)
    {
        cJSON   *row = cJSON_CreateObject();
        int     n = 0;

        if(row == NULL) continue;

        if(vwm->decks[i] != NULL)
            n = vk_deck_count(vwm->decks[i]);

        cJSON_AddNumberToObject(row, "desktop", i);
        cJSON_AddNumberToObject(row, "windows", n);
        cJSON_AddItemToArray(arr, row);
    }

    cJSON_AddItemToObject(data, "desktops", arr);
    ctl_reply(fd, 1, data, NULL);
}

static void
op_list_windows(int fd)
{
    vwm_t   *vwm = vwm_get_instance();
    cJSON   *data;
    cJSON   *arr;
    int     i, j, n;
    int     active;

    if(vwm == NULL)
    {
        ctl_reply(fd, 0, NULL, "no vwm");
        return;
    }

    data = cJSON_CreateObject();
    arr = cJSON_CreateArray();
    if(data == NULL || arr == NULL)
    {
        if(data != NULL) cJSON_Delete(data);
        if(arr != NULL) cJSON_Delete(arr);
        ctl_reply(fd, 0, NULL, "oom");
        return;
    }

    active = vk_screen_get_active_surface(vwm->screen);

    for(i = 0; i < vwm->surface_count; i++)
    {
        vk_widget_t *top;

        if(vwm->decks[i] == NULL) continue;

        top = vk_deck_get_top(vwm->decks[i]);
        n = vk_deck_count(vwm->decks[i]);

        for(j = 0; j < n; j++)
        {
            vk_widget_t *w = vk_deck_get_widget(vwm->decks[i], j);
            cJSON       *row;
            const char  *title;
            int         x, y, ww, wh;
            int         visible;

            if(w == NULL) continue;

            row = cJSON_CreateObject();
            if(row == NULL) continue;

            title = vk_window_get_title(VK_WINDOW(w));
            vk_widget_get_position(w, &x, &y);
            vk_widget_get_metrics(w, &ww, &wh);
            visible = (vk_widget_get_state(w) & VK_STATE_VISIBLE) ? 1 : 0;

            cJSON_AddNumberToObject(row, "id", vk_widget_get_id(w));
            cJSON_AddStringToObject(row, "title",
                (title != NULL) ? title : "");
            cJSON_AddNumberToObject(row, "desktop", i);
            cJSON_AddNumberToObject(row, "x", x);
            cJSON_AddNumberToObject(row, "y", y);
            cJSON_AddNumberToObject(row, "w", ww);
            cJSON_AddNumberToObject(row, "h", wh);
            cJSON_AddBoolToObject(row, "focused",
                (i == active && w == top) ? 1 : 0);
            cJSON_AddBoolToObject(row, "minimized", visible ? 0 : 1);
            cJSON_AddStringToObject(row, "kind",
                (vk_widget_get_userptr(w) != NULL) ? "vterm" : "window");
            cJSON_AddItemToArray(arr, row);
        }
    }

    cJSON_AddItemToObject(data, "windows", arr);
    ctl_reply(fd, 1, data, NULL);
}

static void
op_focused(int fd)
{
    vwm_t       *vwm = vwm_get_instance();
    vk_widget_t *top;
    cJSON       *data;

    if(vwm == NULL || vwm->deck == NULL)
    {
        ctl_reply(fd, 0, NULL, "no vwm");
        return;
    }

    data = cJSON_CreateObject();
    if(data == NULL)
    {
        ctl_reply(fd, 0, NULL, "oom");
        return;
    }

    cJSON_AddNumberToObject(data, "desktop",
        vk_screen_get_active_surface(vwm->screen));

    top = vk_deck_get_top(vwm->deck);
    if(top == NULL)
        cJSON_AddNullToObject(data, "id");
    else
        cJSON_AddNumberToObject(data, "id", vk_widget_get_id(top));

    ctl_reply(fd, 1, data, NULL);
}

static void
op_desktop(int fd, cJSON *req)
{
    int present;
    int n = ctl_json_int(req, "desktop", &present);

    if(!present)
        n = ctl_json_int(req, "n", &present);

    if(!present)
    {
        ctl_reply(fd, 0, NULL, "missing desktop");
        return;
    }

    if(ctl_switch_desktop(n) != 0)
    {
        ctl_reply(fd, 0, NULL, "bad desktop");
        return;
    }

    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_focus(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    int         desk;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, &desk);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    if(ctl_switch_desktop(desk) != 0)
    {
        ctl_reply(fd, 0, NULL, "bad desktop");
        return;
    }

    vk_widget_show(w);
    vk_deck_set_top(vwm_get_instance()->decks[desk], w);
    vk_screen_refresh(vwm_get_instance()->screen);

    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_close(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, NULL);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    vwm_default_WINDOW_CLOSE(w);
    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_minimize(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, NULL);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    vwm_minimize_window(w);
    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_restore(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    int         desk;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, &desk);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    vwm_restore_window(w);
    ctl_switch_desktop(desk);
    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_move(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    int         x, y, has_x, has_y, has_dx, has_dy;
    int         dx, dy;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, NULL);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    vk_widget_get_position(w, &x, &y);
    dx = ctl_json_int(req, "dx", &has_dx);
    dy = ctl_json_int(req, "dy", &has_dy);
    if(has_dx) x += dx;
    if(has_dy) y += dy;

    {
        int ax = ctl_json_int(req, "x", &has_x);
        int ay = ctl_json_int(req, "y", &has_y);

        if(has_x) x = ax;
        if(has_y) y = ay;
    }

    vk_widget_move(w, x, y);
    vk_screen_refresh(vwm_get_instance()->screen);
    ctl_reply(fd, 1, NULL, NULL);
}

static void
op_resize(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    int         ww, wh, has;
    int         n;

    if(!present || id == 0)
    {
        ctl_reply(fd, 0, NULL, "missing id");
        return;
    }

    w = ctl_find_id(id, NULL);
    if(w == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown id");
        return;
    }

    vk_widget_get_metrics(w, &ww, &wh);

    n = ctl_json_int(req, "dw", &has);
    if(has) ww += n;
    n = ctl_json_int(req, "dh", &has);
    if(has) wh += n;
    n = ctl_json_int(req, "width", &has);
    if(has) ww = n;
    n = ctl_json_int(req, "height", &has);
    if(has) wh = n;

    if(ww < 3) ww = 3;
    if(wh < 3) wh = 3;

    vk_widget_resize(w, ww, wh);
    vk_window_update(VK_WINDOW(w));
    vk_screen_refresh(vwm_get_instance()->screen);
    ctl_reply(fd, 1, NULL, NULL);
}

static char **
ctl_args_from_json(cJSON *req, const char *bin)
{
    cJSON   *arr;
    char    **argv;
    int     n = 0;
    int     i;

    arr = cJSON_GetObjectItemCaseSensitive(req, "args");
    if(cJSON_IsArray(arr))
        n = cJSON_GetArraySize(arr);

    argv = calloc((size_t)n + 2, sizeof(char *));
    if(argv == NULL) return NULL;

    argv[0] = strdup(bin);
    if(argv[0] == NULL)
    {
        free(argv);
        return NULL;
    }

    for(i = 0; i < n; i++)
    {
        cJSON *it = cJSON_GetArrayItem(arr, i);

        if(!cJSON_IsString(it) || it->valuestring == NULL)
            argv[i + 1] = strdup("");
        else
            argv[i + 1] = strdup(it->valuestring);

        if(argv[i + 1] == NULL)
        {
            strfreev(argv);
            return NULL;
        }
    }

    return argv;
}

static void
op_launch(int fd, cJSON *req)
{
    vwm_t           *vwm = vwm_get_instance();
    vwm_module_t    *tmpl;
    vwm_module_t    *clone;
    vk_window_t     *window;
    const char      *bin;
    const char      *profile;
    char            **argv;
    int             present;
    int             w, h, sb, home;
    cJSON           *data;

    if(vwm == NULL)
    {
        ctl_reply(fd, 0, NULL, "no vwm");
        return;
    }

    bin = ctl_json_str(req, "bin");
    if(bin == NULL || bin[0] == '\0')
    {
        ctl_reply(fd, 0, NULL, "missing bin");
        return;
    }

    profile = ctl_json_str(req, "profile");
    if(profile == NULL || profile[0] == '\0')
        profile = "vterm-color";

    tmpl = vwm_module_find_by_name((char *)profile);
    if(tmpl == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown profile");
        return;
    }

    clone = vwm_module_clone(tmpl);
    if(clone == NULL)
    {
        ctl_reply(fd, 0, NULL, "clone failed");
        return;
    }

    w = ctl_json_int(req, "width", &present);
    clone->term_width = present ? w : 80;
    h = ctl_json_int(req, "height", &present);
    clone->term_height = present ? h : 25;
    sb = ctl_json_int(req, "scrollback", &present);
    clone->scrollback = present ? sb : 0;
    home = ctl_json_int(req, "start_home", &present);
    clone->start_home = present ? (home != 0) : false;

    argv = ctl_args_from_json(req, bin);
    if(argv == NULL)
    {
        free(clone);
        ctl_reply(fd, 0, NULL, "oom");
        return;
    }

    vwm_module_configure(clone, (char *)bin, argv);
    window = vwm_module_exec(clone);
    strfreev(argv);

    if(window == NULL)
    {
        ctl_reply(fd, 0, NULL, "launch failed");
        return;
    }

    vk_deck_add_widget(vwm->deck, VK_WIDGET(window), VK_DECK_TOP);
    vk_screen_refresh(vwm->screen);

    data = cJSON_CreateObject();
    if(data != NULL)
        cJSON_AddNumberToObject(data, "id",
            vk_widget_get_id(VK_WIDGET(window)));

    ctl_reply(fd, 1, data, NULL);
}

static void
op_launch_app(int fd, cJSON *req)
{
    vwm_t           *vwm = vwm_get_instance();
    vwm_module_t    *mod;
    const char      *title;
    vk_window_t     *window;
    cJSON           *data;

    if(vwm == NULL)
    {
        ctl_reply(fd, 0, NULL, "no vwm");
        return;
    }

    title = ctl_json_str(req, "title");
    if(title == NULL || title[0] == '\0')
    {
        ctl_reply(fd, 0, NULL, "missing title");
        return;
    }

    mod = vwm_module_find_by_title((char *)title);
    if(mod == NULL)
    {
        ctl_reply(fd, 0, NULL, "unknown app");
        return;
    }

    if(mod->fd_argv != NULL)
    {
        ctl_reply(fd, 0, NULL, "app requires a file picker");
        return;
    }

    window = vwm_module_exec(mod);
    if(window == NULL)
    {
        ctl_reply(fd, 0, NULL, "launch failed");
        return;
    }

    vk_deck_add_widget(vwm->deck, VK_WIDGET(window), VK_DECK_TOP);
    vk_screen_refresh(vwm->screen);

    data = cJSON_CreateObject();
    if(data != NULL)
        cJSON_AddNumberToObject(data, "id",
            vk_widget_get_id(VK_WIDGET(window)));

    ctl_reply(fd, 1, data, NULL);
}

static void
ctl_dispatch(int fd, cJSON *req)
{
    const char  *op;

    op = ctl_json_str(req, "op");
    if(op == NULL)
    {
        ctl_reply(fd, 0, NULL, "missing op");
        return;
    }

    if(strcmp(op, "ping") == 0)             op_ping(fd);
    else if(strcmp(op, "list-windows") == 0)  op_list_windows(fd);
    else if(strcmp(op, "list-desktops") == 0) op_list_desktops(fd);
    else if(strcmp(op, "focused") == 0)       op_focused(fd);
    else if(strcmp(op, "desktop") == 0)       op_desktop(fd, req);
    else if(strcmp(op, "focus") == 0)         op_focus(fd, req);
    else if(strcmp(op, "close") == 0)         op_close(fd, req);
    else if(strcmp(op, "minimize") == 0)      op_minimize(fd, req);
    else if(strcmp(op, "restore") == 0)       op_restore(fd, req);
    else if(strcmp(op, "move") == 0)          op_move(fd, req);
    else if(strcmp(op, "resize") == 0)        op_resize(fd, req);
    else if(strcmp(op, "launch") == 0)        op_launch(fd, req);
    else if(strcmp(op, "launch-app") == 0)    op_launch_app(fd, req);
    else
        ctl_reply(fd, 0, NULL, "unknown op");
}
