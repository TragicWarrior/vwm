#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <wchar.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include <vdk.h>
#include <vterm.h>

#include "cJSON.h"
#include "vwm.h"
#include "private.h"
#include "modules.h"
#include "winman.h"
#include "strings.h"
#include "screenshot.h"
#include "ctl.h"
#include "modules/vwmterm3/vwmterm.h"

#define VWM_CTL_MAX_REQ     (256 * 1024)

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

static int
ctl_json_bool(cJSON *req, const char *key, int *present)
{
    cJSON   *item;

    if(present != NULL) *present = 0;
    item = cJSON_GetObjectItemCaseSensitive(req, key);
    if(item == NULL)
        return 0;

    if(present != NULL) *present = 1;
    if(cJSON_IsTrue(item)) return 1;
    if(cJSON_IsFalse(item)) return 0;
    if(cJSON_IsNumber(item)) return item->valuedouble != 0;

    return 0;
}

static vterm_t *
ctl_widget_vterm(vk_widget_t *w)
{
    vwmterm_data_t  *data;

    if(w == NULL) return NULL;
    data = (vwmterm_data_t *)vk_widget_get_userptr(w);
    if(data == NULL) return NULL;

    return data->vterm;
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
            cJSON_AddBoolToObject(row, "attention",
                vwm_attention_is(w) ? 1 : 0);
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

/*
    Screen geometry for clamp/fit.  Returns 0, or -1 if the SCREEN is
    not usable.  Height includes the panel (row 0) and status (last
    row); callers that size a window treat those as reserved.
*/
static int
ctl_screen_size(int *scr_w, int *scr_h)
{
    vwm_t   *vwm = vwm_get_instance();
    int     h, w;

    if(vwm == NULL || vwm->screen == NULL) return -1;

    getmaxyx(vk_screen_get_window(vwm->screen), h, w);
    if(w < 3 || h < 3) return -1;

    if(scr_w != NULL) *scr_w = w;
    if(scr_h != NULL) *scr_h = h;
    return 0;
}

/*
    Cap a resize so the window stays on-screen from its current origin.
    Right edge must not pass COLS; bottom must stay off the status row.
    A size that would hang off is how windows became unmovable (mvwin
    then refuses every later move).  Floor is 3, same as the resize
    hotkeys / the old ctl minimum.
*/
static void
ctl_clamp_metrics(vk_widget_t *w, int *ww, int *wh)
{
    int x, y, scr_w, scr_h, max_w, max_h;

    if(w == NULL || ww == NULL || wh == NULL) return;
    if(ctl_screen_size(&scr_w, &scr_h) != 0) return;

    vk_widget_get_position(w, &x, &y);

    max_w = scr_w - x;
    max_h = (scr_h - 1) - y;
    if(max_w < 3) max_w = 3;
    if(max_h < 3) max_h = 3;

    if(*ww > max_w) *ww = max_w;
    if(*wh > max_h) *wh = max_h;
    if(*ww < 3) *ww = 3;
    if(*wh < 3) *wh = 3;
}

/* After launch: same on-screen fit as restore.  Skips fullscreen. */
static void
ctl_fit_new_window(vk_widget_t *w)
{
    int scr_w, scr_h;

    if(w == NULL) return;
    if(vk_widget_get_state(w) & VK_STATE_NORESIZE) return;
    if(ctl_screen_size(&scr_w, &scr_h) != 0) return;

    vwm_fit_window_onscreen(w, scr_w, scr_h);
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

    ctl_clamp_metrics(w, &ww, &wh);

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

    vwm_deck_add_window(vwm->deck, VK_WIDGET(window), VK_DECK_TOP);
    ctl_fit_new_window(VK_WIDGET(window));
    if(ctl_json_bool(req, "attention", NULL))
        vwm_attention_set(VK_WIDGET(window));
    vk_screen_refresh(vwm->screen);

    data = cJSON_CreateObject();
    if(data != NULL)
        cJSON_AddNumberToObject(data, "id",
            vk_widget_get_id(VK_WIDGET(window)));

    ctl_reply(fd, 1, data, NULL);
}

static void
ctl_title_norm(const char *in, char *out, size_t out_sz)
{
    size_t  n;
    char    *end;

    if(out_sz == 0) return;
    out[0] = '\0';
    if(in == NULL) return;

    while(*in == ' ' || *in == '\t')
        in++;

    n = strlen(in);
    if(n >= out_sz)
        n = out_sz - 1;
    memcpy(out, in, n);
    out[n] = '\0';

    if((out[0] == '"' || out[0] == '\'') && n >= 2 && out[n - 1] == out[0])
    {
        memmove(out, out + 1, n - 2);
        out[n - 2] = '\0';
    }

    end = out + strlen(out);
    while(end > out && (end[-1] == ' ' || end[-1] == '\t'))
    {
        end--;
        *end = '\0';
    }
}

static const char *
ctl_zone_name(int zone)
{
    switch(zone)
    {
        case MODULE_ZONE_USER:  return "user";
        case MODULE_ZONE_APP:   return "app";
        default:                return "core";
    }
}

static vwm_module_t *
ctl_find_app(const char *title)
{
    vwm_t               *vwm = vwm_get_instance();
    struct list_head    *pos;
    vwm_module_t        *best = NULL;
    int                 best_score = -1;
    char                want[NAME_MAX];

    if(vwm == NULL || title == NULL)
        return NULL;

    ctl_title_norm(title, want, sizeof(want));
    if(want[0] == '\0')
        return NULL;

    list_for_each(pos, &vwm->module_list)
    {
        vwm_module_t    *mod;
        char            have[NAME_MAX];
        int             score = 0;

        mod = list_entry(pos, vwm_module_t, list);
        ctl_title_norm(mod->title, have, sizeof(have));
        if(strcasecmp(have, want) != 0)
            continue;

        if(strcmp(have, want) == 0)
            score += 1;
        if(vwm_module_get_zone(mod) == MODULE_ZONE_USER)
            score += 2;
        else if(vwm_module_get_zone(mod) == MODULE_ZONE_APP)
            score += 1;

        if(score > best_score)
        {
            best_score = score;
            best = mod;
        }
    }

    return best;
}

static void
op_list_apps(int fd)
{
    vwm_t               *vwm = vwm_get_instance();
    struct list_head    *pos;
    cJSON               *data;
    cJSON               *arr;

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

    list_for_each(pos, &vwm->module_list)
    {
        vwm_module_t    *mod;
        cJSON           *row;
        const char      *type;

        mod = list_entry(pos, vwm_module_t, list);
        if(mod->title[0] == '\0')
            continue;

        row = cJSON_CreateObject();
        if(row == NULL) continue;

        type = vwm_module_type_string(vwm_module_get_type(mod));

        cJSON_AddStringToObject(row, "title", mod->title);
        cJSON_AddStringToObject(row, "name", mod->name);
        cJSON_AddStringToObject(row, "zone",
            ctl_zone_name(vwm_module_get_zone(mod)));
        cJSON_AddStringToObject(row, "type",
            (type != NULL) ? type : "");
        cJSON_AddBoolToObject(row, "hidden",
            vwm_module_is_hidden(mod) ? 1 : 0);
        cJSON_AddBoolToObject(row, "picker",
            (mod->fd_argv != NULL) ? 1 : 0);
        cJSON_AddItemToArray(arr, row);
    }

    cJSON_AddItemToObject(data, "apps", arr);
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

    mod = ctl_find_app(title);
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

    vwm_deck_add_window(vwm->deck, VK_WIDGET(window), VK_DECK_TOP);
    ctl_fit_new_window(VK_WIDGET(window));
    if(ctl_json_bool(req, "attention", NULL))
        vwm_attention_set(VK_WIDGET(window));
    vk_screen_refresh(vwm->screen);

    data = cJSON_CreateObject();
    if(data != NULL)
        cJSON_AddNumberToObject(data, "id",
            vk_widget_get_id(VK_WIDGET(window)));

    ctl_reply(fd, 1, data, NULL);
}

/*
    xterm function-key sequences.  send-keys must write these bytes to
    the pty -- not ncurses KEY_F(n).  KEY_F(10) is 274 (0x0112); if
    libvterm's keymap misses it, the 16-bit pack fallback emits
    0x12 0x01 and the child never sees F10.  Typed CSI \e[21~ works.
*/
static const char *
ctl_fkey_seq(int fn)
{
    static const char *const seq[] =
    {
        NULL,
        "\x1bOP",
        "\x1bOQ",
        "\x1bOR",
        "\x1bOS",
        "\x1b[15~",
        "\x1b[17~",
        "\x1b[18~",
        "\x1b[19~",
        "\x1b[20~",
        "\x1b[21~",
        "\x1b[23~",
        "\x1b[24~",
    };

    if(fn < 1 || fn > 12)
        return NULL;

    return seq[fn];
}

static int
ctl_write_bytes(vterm_t *vterm, const unsigned char *p, size_t n)
{
    size_t  i;

    for(i = 0; i < n; i++)
    {
        if(vterm_write_pipe(vterm, (uint32_t)p[i]) < 0)
            return -1;
    }

    return 0;
}

static int
ctl_parse_key(const char *name, uint32_t *out, const char **seq)
{
    char        fold[32];
    size_t      i, n;
    const char  *p;

    if(seq != NULL)
        *seq = NULL;

    if(name == NULL || name[0] == '\0' || out == NULL)
        return -1;

    n = strlen(name);
    if(n >= sizeof(fold))
        return -1;

    for(i = 0; i < n; i++)
        fold[i] = (char)tolower((unsigned char)name[i]);
    fold[n] = '\0';

    if((fold[0] == 'c' && fold[1] == '-') ||
        fold[0] == '^' ||
        strncmp(fold, "ctrl-", 5) == 0)
    {
        if(fold[0] == '^')
            p = fold + 1;
        else if(fold[0] == 'c' && fold[1] == '-')
            p = fold + 2;
        else
            p = fold + 5;

        if(p[0] != '\0' && p[1] == '\0')
        {
            unsigned char c = (unsigned char)p[0];

            if(c >= 'a' && c <= 'z')
            {
                *out = (uint32_t)(c - 'a' + 1);
                return 0;
            }
            if(c == '[') { *out = 27; return 0; }
            if(c == '\\') { *out = 28; return 0; }
            if(c == ']') { *out = 29; return 0; }
            if(c == '^') { *out = 30; return 0; }
            if(c == '_' || c == '-') { *out = 31; return 0; }
            if(c == '?') { *out = 127; return 0; }
            if(c == ' ' || c == '@') { *out = 0; return 0; }
        }

        return -1;
    }

    if((fold[0] == 'm' && fold[1] == '-') ||
        (fold[0] == 'a' && fold[1] == '-') ||
        strncmp(fold, "alt-", 4) == 0)
    {
        if(strncmp(fold, "alt-", 4) == 0)
            p = fold + 4;
        else
            p = fold + 2;

        if(p[0] != '\0' && p[1] == '\0')
        {
            *out = 27u | ((uint32_t)(unsigned char)p[0] << 8);
            return 0;
        }

        return -1;
    }

    if(strcmp(fold, "enter") == 0 || strcmp(fold, "return") == 0 ||
        strcmp(fold, "cr") == 0)
    {
        *out = '\r';
        return 0;
    }
    if(strcmp(fold, "lf") == 0 || strcmp(fold, "nl") == 0)
    {
        *out = '\n';
        return 0;
    }
    if(strcmp(fold, "tab") == 0)
    {
        *out = '\t';
        return 0;
    }
    if(strcmp(fold, "esc") == 0 || strcmp(fold, "escape") == 0)
    {
        *out = 27;
        return 0;
    }
    if(strcmp(fold, "space") == 0)
    {
        *out = ' ';
        return 0;
    }
    if(strcmp(fold, "bs") == 0 || strcmp(fold, "backspace") == 0 ||
        strcmp(fold, "bspace") == 0)
    {
        *out = (uint32_t)KEY_BACKSPACE;
        return 0;
    }
    if(strcmp(fold, "dc") == 0 || strcmp(fold, "del") == 0 ||
        strcmp(fold, "delete") == 0)
    {
        *out = (uint32_t)KEY_DC;
        return 0;
    }
    if(strcmp(fold, "up") == 0)
    {
        *out = (uint32_t)KEY_UP;
        return 0;
    }
    if(strcmp(fold, "down") == 0 || strcmp(fold, "dn") == 0)
    {
        *out = (uint32_t)KEY_DOWN;
        return 0;
    }
    if(strcmp(fold, "left") == 0)
    {
        *out = (uint32_t)KEY_LEFT;
        return 0;
    }
    if(strcmp(fold, "right") == 0)
    {
        *out = (uint32_t)KEY_RIGHT;
        return 0;
    }
    if(strcmp(fold, "home") == 0)
    {
        *out = (uint32_t)KEY_HOME;
        return 0;
    }
    if(strcmp(fold, "end") == 0)
    {
        *out = (uint32_t)KEY_END;
        return 0;
    }
    if(strcmp(fold, "pageup") == 0 || strcmp(fold, "pgup") == 0 ||
        strcmp(fold, "ppage") == 0)
    {
        *out = (uint32_t)KEY_PPAGE;
        return 0;
    }
    if(strcmp(fold, "pagedown") == 0 || strcmp(fold, "pgdn") == 0 ||
        strcmp(fold, "npage") == 0)
    {
        *out = (uint32_t)KEY_NPAGE;
        return 0;
    }
    if(strcmp(fold, "ic") == 0 || strcmp(fold, "insert") == 0)
    {
        *out = (uint32_t)KEY_IC;
        return 0;
    }

    if(fold[0] == 'f' && fold[1] >= '1' && fold[1] <= '9')
    {
        int fn = atoi(fold + 1);

        if(fn >= 1 && fn <= 12)
        {
            *out = (uint32_t)KEY_F(fn);
            if(seq != NULL)
                *seq = ctl_fkey_seq(fn);
            return 0;
        }
    }

    if(n == 1)
    {
        *out = (uint32_t)(unsigned char)name[0];
        return 0;
    }

    return -1;
}

static void
op_send_keys(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    vterm_t     *vterm;
    const char  *text;
    cJSON       *keys;
    int         do_type;
    int         do_enter;
    size_t      wrote = 0;
    int         i, n;
    cJSON       *data;

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

    vterm = ctl_widget_vterm(w);
    if(vterm == NULL)
    {
        ctl_reply(fd, 0, NULL, "not a vterm");
        return;
    }

    text = ctl_json_str(req, "text");
    keys = cJSON_GetObjectItemCaseSensitive(req, "keys");
    do_type = ctl_json_bool(req, "type", NULL);
    do_enter = ctl_json_bool(req, "enter", NULL);

    if((text == NULL || text[0] == '\0') && !cJSON_IsArray(keys) && !do_enter)
    {
        ctl_reply(fd, 0, NULL, "missing text or keys");
        return;
    }

    if(text != NULL && text[0] != '\0')
    {
        size_t  len = strlen(text);

        if(do_type)
        {
            for(i = 0; i < (int)len; i++)
            {
                if(vterm_write_pipe(vterm,
                    (uint32_t)(unsigned char)text[i]) < 0)
                {
                    ctl_reply(fd, 0, NULL, "write failed");
                    return;
                }
            }
        }
        else if(vterm_write_data(vterm, text, len) < 0)
        {
            ctl_reply(fd, 0, NULL, "write failed");
            return;
        }

        wrote += len;
    }

    if(cJSON_IsArray(keys))
    {
        n = cJSON_GetArraySize(keys);
        for(i = 0; i < n; i++)
        {
            cJSON       *it = cJSON_GetArrayItem(keys, i);
            uint32_t    key;
            const char  *seq = NULL;

            if(!cJSON_IsString(it) || it->valuestring == NULL)
            {
                ctl_reply(fd, 0, NULL, "bad key");
                return;
            }

            if(ctl_parse_key(it->valuestring, &key, &seq) != 0)
            {
                ctl_reply(fd, 0, NULL, "unknown key");
                return;
            }

            if(seq != NULL)
            {
                if(ctl_write_bytes(vterm,
                    (const unsigned char *)seq, strlen(seq)) < 0)
                {
                    ctl_reply(fd, 0, NULL, "write failed");
                    return;
                }
            }
            else if(vterm_write_pipe(vterm, key) < 0)
            {
                ctl_reply(fd, 0, NULL, "write failed");
                return;
            }

            wrote++;
        }
    }

    if(do_enter)
    {
        if(vterm_write_pipe(vterm, (uint32_t)'\r') < 0)
        {
            ctl_reply(fd, 0, NULL, "write failed");
            return;
        }
        wrote++;
    }

    data = cJSON_CreateObject();
    if(data != NULL)
        cJSON_AddNumberToObject(data, "bytes", (double)wrote);

    ctl_reply(fd, 1, data, NULL);
}

struct ctl_txt
{
    char    *p;
    size_t  len;
    size_t  cap;
};

static int
ctl_txt_grow(struct ctl_txt *b, size_t add)
{
    size_t  need;
    size_t  ncap;
    char    *np;

    need = b->len + add + 1;
    if(need <= b->cap)
        return 0;

    ncap = (b->cap != 0) ? b->cap : 4096;
    while(ncap < need)
        ncap *= 2;

    np = realloc(b->p, ncap);
    if(np == NULL)
        return -1;

    b->p = np;
    b->cap = ncap;
    return 0;
}

static int
ctl_txt_putc(struct ctl_txt *b, char c)
{
    if(ctl_txt_grow(b, 1) != 0)
        return -1;

    b->p[b->len++] = c;
    b->p[b->len] = '\0';
    return 0;
}

static void
ctl_cells_free(vterm_cell_t **cells, int rows)
{
    int r;

    if(cells == NULL)
        return;

    for(r = 0; r < rows; r++)
        free(cells[r]);
    free(cells);
}

static int
ctl_cells_append(struct ctl_txt *b, vterm_cell_t **cells,
    int rows, int cols, int r0, int n)
{
    int     r, c;
    int     last;
    char    mb[MB_LEN_MAX];
    int     len;

    if(cells == NULL || n <= 0)
        return 0;

    last = r0 + n;
    if(r0 < 0) r0 = 0;
    if(last > rows) last = rows;

    for(r = r0; r < last; r++)
    {
        size_t  line_start = b->len;

        if(r > r0 || b->len > 0)
        {
            if(ctl_txt_putc(b, '\n') != 0)
                return -1;
            line_start = b->len;
        }

        for(c = 0; c < cols; c++)
        {
            wchar_t wch = cells[r][c].wch[0];

            if(c > 0
                && cells[r][c - 1].wch[0] > 0x7F
                && wcwidth(cells[r][c - 1].wch[0]) == 2)
            {
                continue;
            }

            if(wch == 0)
                wch = L' ';

            len = wctomb(mb, wch);
            if(len <= 0)
                continue;

            if(ctl_txt_grow(b, (size_t)len) != 0)
                return -1;

            memcpy(b->p + b->len, mb, (size_t)len);
            b->len += (size_t)len;
            b->p[b->len] = '\0';
        }

        while(b->len > line_start && b->p[b->len - 1] == ' ')
        {
            b->len--;
            b->p[b->len] = '\0';
        }
    }

    return 0;
}

static char *
ctl_vterm_capture(vterm_t *vterm, int hist_lines, int *out_rows, int *out_cols)
{
    struct ctl_txt  b;
    vterm_cell_t    **cells;
    int             rows, cols;
    int             used;
    int             width, height;
    int             remaining;
    int             off;
    int             alt;

    memset(&b, 0, sizeof(b));
    vterm_get_size(vterm, &width, &height);
    used = vterm_get_history_used(vterm);
    alt = (vterm_get_active_buffer(vterm) == VTERM_BUF_ALTERNATE);

    if(alt || hist_lines == 0)
        remaining = 0;
    else if(hist_lines < 0 || hist_lines > used)
        remaining = used;
    else
        remaining = hist_lines;

    off = remaining;
    while(remaining > 0)
    {
        int take;

        cells = vterm_copy_scrollback(vterm, off, &rows, &cols);
        if(cells == NULL)
        {
            free(b.p);
            return NULL;
        }

        take = (remaining < rows) ? remaining : rows;
        if(ctl_cells_append(&b, cells, rows, cols, 0, take) != 0)
        {
            ctl_cells_free(cells, rows);
            free(b.p);
            return NULL;
        }

        ctl_cells_free(cells, rows);
        remaining -= take;
        off -= take;
    }

    cells = vterm_copy_buffer(vterm, &rows, &cols);
    if(cells == NULL)
    {
        free(b.p);
        return NULL;
    }

    if(ctl_cells_append(&b, cells, rows, cols, 0, rows) != 0)
    {
        ctl_cells_free(cells, rows);
        free(b.p);
        return NULL;
    }

    ctl_cells_free(cells, rows);

    if(out_cols != NULL)
        *out_cols = width;
    if(out_rows != NULL)
    {
        int hist = 0;

        if(!alt)
        {
            if(hist_lines < 0 || hist_lines > used)
                hist = used;
            else if(hist_lines > 0)
                hist = hist_lines;
        }
        *out_rows = hist + height;
    }

    if(b.p == NULL)
    {
        b.p = strdup("");
        if(b.p == NULL)
            return NULL;
    }

    return b.p;
}

static int
ctl_json_scrollback(cJSON *req)
{
    cJSON   *item;

    item = cJSON_GetObjectItemCaseSensitive(req, "scrollback");
    if(item == NULL)
        return 0;
    if(cJSON_IsTrue(item))
        return -1;
    if(cJSON_IsFalse(item))
        return 0;
    if(cJSON_IsNumber(item))
        return (int)item->valuedouble;

    return 0;
}

static void
op_capture(int fd, cJSON *req)
{
    int         present;
    uint32_t    id = ctl_json_u32(req, "id", &present);
    vk_widget_t *w;
    vterm_t     *vterm;
    const char  *path;
    char        *text;
    int         hist;
    int         rows, cols;
    int         used;
    int         alt;
    cJSON       *data;
    FILE        *fp;

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

    vterm = ctl_widget_vterm(w);
    if(vterm == NULL)
    {
        ctl_reply(fd, 0, NULL, "not a vterm");
        return;
    }

    hist = ctl_json_scrollback(req);
    path = ctl_json_str(req, "path");
    text = ctl_vterm_capture(vterm, hist, &rows, &cols);
    if(text == NULL)
    {
        ctl_reply(fd, 0, NULL, "capture failed");
        return;
    }

    used = vterm_get_history_used(vterm);
    alt = (vterm_get_active_buffer(vterm) == VTERM_BUF_ALTERNATE);

    if(path != NULL && path[0] != '\0')
    {
        fp = fopen(path, "w");
        if(fp == NULL)
        {
            free(text);
            ctl_reply(fd, 0, NULL, "open path failed");
            return;
        }

        if(fputs(text, fp) == EOF)
        {
            fclose(fp);
            free(text);
            ctl_reply(fd, 0, NULL, "write path failed");
            return;
        }

        fclose(fp);
    }

    data = cJSON_CreateObject();
    if(data == NULL)
    {
        free(text);
        ctl_reply(fd, 0, NULL, "oom");
        return;
    }

    cJSON_AddNumberToObject(data, "id", id);
    cJSON_AddNumberToObject(data, "rows", rows);
    cJSON_AddNumberToObject(data, "cols", cols);
    cJSON_AddNumberToObject(data, "history_used", used);
    cJSON_AddStringToObject(data, "buffer",
        alt ? "alternate" : "standard");

    if(path != NULL && path[0] != '\0')
        cJSON_AddStringToObject(data, "path", path);
    else
        cJSON_AddStringToObject(data, "text", text);

    free(text);
    ctl_reply(fd, 1, data, NULL);
}

static const char *
ctl_shot_err(int code)
{
    switch(code)
    {
        case VWM_SHOT_OK:       return NULL;
        case VWM_SHOT_ERR_FONT: return "font";
        case VWM_SHOT_ERR_GLYPH:return "glyph";
        case VWM_SHOT_ERR_ALLOC:return "oom";
        case VWM_SHOT_ERR_PNG:  return "png";
        default:                return "screenshot failed";
    }
}

static void
op_screenshot(int fd, cJSON *req)
{
    const char  *target_s;
    const char  *path;
    char        gen[PATH_MAX];
    int         target;
    int         code;
    cJSON       *data;

    target_s = ctl_json_str(req, "target");
    if(target_s == NULL || target_s[0] == '\0' ||
        strcmp(target_s, "screen") == 0)
    {
        target = VWM_SHOT_SCREEN;
        target_s = "screen";
    }
    else if(strcmp(target_s, "top") == 0)
        target = VWM_SHOT_TOP;
    else
    {
        ctl_reply(fd, 0, NULL, "bad target");
        return;
    }

    path = ctl_json_str(req, "path");
    if(path == NULL || path[0] == '\0')
    {
        snprintf(gen, sizeof(gen), "/tmp/vwm-shot-%d-%d-%ld.png",
            (int)getuid(), (int)getpid(), (long)time(NULL));
        path = gen;
    }

    code = vwm_screenshot_save(target, path);
    if(code != VWM_SHOT_OK)
    {
        ctl_reply(fd, 0, NULL, ctl_shot_err(code));
        return;
    }

    data = cJSON_CreateObject();
    if(data != NULL)
    {
        cJSON_AddStringToObject(data, "path", path);
        cJSON_AddStringToObject(data, "target", target_s);
    }

    ctl_reply(fd, 1, data, NULL);
}

static void
op_attention(int fd, cJSON *req)
{
    int         present;
    int         off;
    uint32_t    id;
    vk_widget_t *w;

    off = ctl_json_bool(req, "off", NULL);
    id = ctl_json_u32(req, "id", &present);

    if(off)
    {
        if(present && id != 0)
        {
            w = ctl_find_id(id, NULL);
            if(w == NULL)
            {
                ctl_reply(fd, 0, NULL, "unknown id");
                return;
            }
            vwm_attention_clear(w);
        }
        else
            vwm_attention_clear(NULL);

        vk_screen_refresh(vwm_get_instance()->screen);
        ctl_reply(fd, 1, NULL, NULL);
        return;
    }

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

    vwm_attention_set(w);
    ctl_reply(fd, 1, NULL, NULL);
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
    else if(strcmp(op, "list-apps") == 0)     op_list_apps(fd);
    else if(strcmp(op, "launch-app") == 0)    op_launch_app(fd, req);
    else if(strcmp(op, "send-keys") == 0)     op_send_keys(fd, req);
    else if(strcmp(op, "capture") == 0)       op_capture(fd, req);
    else if(strcmp(op, "screenshot") == 0)    op_screenshot(fd, req);
    else if(strcmp(op, "attention") == 0)     op_attention(fd, req);
    else
        ctl_reply(fd, 0, NULL, "unknown op");
}
