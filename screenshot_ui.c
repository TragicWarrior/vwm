#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include <vdk.h>
#include <vkmio.h>
#include <ncursesw/curses.h>

#include "vwm.h"
#include "screenshot.h"
#include "private.h"
#include "panel.h"
#include "winman.h"

#define SAVE_W          64
#define SAVE_H          22

/* capture targets / prompt actions */
enum
{
    TARGET_SCREEN = 0,
    TARGET_WINDOW,
    TARGET_CANCEL
};

/* session states */
enum
{
    ST_PROMPT = 0,
    ST_SAVE,
    ST_RESULT
};

/* save-dialog focus stops, cycled with Tab (top-to-bottom layout order) */
enum
{
    SF_FILENAME = 0,
    SF_FILEDIALOG,
    SF_OK,
    SF_CANCEL,
    SF_COUNT
};

typedef struct
{
    int             state;
    int             target;
    int             surface;        /* surface the overlay is attached to  */

    vk_window_t     *window;        /* the single surface-overlay window   */
    vk_box_t        *client_box;    /* current window child box           */

    /* prompt state (a decked popup message box) */
    vk_popup_t      *prompt_popup;
    int             prompt_btn;     /* 0=screen, 1=window, 2=cancel        */

    /* save state */
    vk_filedialog_t *fd;
    vk_input_t      *fname;
    vk_label_t      *hint;
    int             save_focus;

    /* double-click tracking for the file list */
    struct timespec last_click_time;
    int             last_click_item;
}
scrshot_session_t;

static scrshot_session_t    *s_session = NULL;

/* set while the module destroys its own window, so the ON_CLOSE handler
   does not also tear the session down (which would double-free) */
static int                  s_module_close = 0;

/* forward declarations */
static int          session_kmio(vk_object_t *object, int32_t keystroke);
static int          scrshot_on_close(vk_object_t *object, int event,
                        void *anything);
static void         handle_mouse(MEVENT *me);
static vk_window_t* build_prompt(scrshot_session_t *s);
static vk_window_t* build_save(scrshot_session_t *s);
static vk_window_t* build_result(const char *msg);
static void         swap_window(vk_window_t *new_win);
static void         close_session(void);
static void         begin_save(int target);
static void         do_capture_and_save(void);
static void         refresh_session(scrshot_session_t *s);
static void         update_save_focus(scrshot_session_t *s);
static void         update_prompt_focus(scrshot_session_t *s);
static void         prompt_activate(scrshot_session_t *s, int btn);

/* ── small helpers ──────────────────────────────────────────────────────── */

static void
center_window(vk_window_t *win)
{
    vwm_t   *vwm = vwm_get_instance();
    int     scr_w, scr_h, w = 0, h = 0, px, py;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
    vk_widget_get_metrics(VK_WIDGET(win), &w, &h);

    px = (scr_w - w) / 2;
    py = (scr_h - h) / 2;
    if(px < 0) px = 0;
    if(py < 0) py = 0;

    vk_widget_move(VK_WIDGET(win), px, py);
}

static void
refresh_session(scrshot_session_t *s)
{
    vwm_t *vwm = vwm_get_instance();

    if(s->prompt_popup != NULL)
    {
        vk_popup_update(s->prompt_popup);
        vk_screen_refresh(vwm->screen);
        return;
    }

    /*
        the filedialog is a nested box; vk_box_update() on the client only
        blits its composer, so it must be re-rendered first or interactions
        (selection, navigation) won't show.
    */
    if(s->fd != NULL) vk_filedialog_update(s->fd);
    if(s->client_box != NULL) vk_box_update(s->client_box);
    if(s->window != NULL) vk_window_update(s->window);

    vk_screen_refresh(vwm->screen);
}

/* create a non-resizable window with the given border color */
static vk_window_t*
make_window(int w, int h, const char *title, short fg, short bg)
{
    vk_window_t *win;
    uint32_t    st;

    win = vk_window_create(w, h);
    vk_window_set_title(win, title);
    vk_window_set_border_style(win, VK_BORDER_SINGLE);
    vk_window_set_border_colors(win, fg, bg);
    vk_window_set_border_attrs(win, A_BOLD);

    st = vk_widget_get_state(VK_WIDGET(win));
    vk_widget_set_state(VK_WIDGET(win), st | VK_STATE_NORESIZE);

    /* clean up if the window is closed via its [X] / the window manager */
    vk_object_register_event(VK_OBJECT(win), VWM_EVENT_ON_CLOSE,
        scrshot_on_close, NULL);

    return win;
}

/* ── prompt window ──────────────────────────────────────────────────────── */

/* center src within a field of `width` columns (pads with spaces) */
static void
center_pad(char *dst, size_t dstsz, const char *src, int width)
{
    int len = (int)strlen(src);
    int total = (width > len) ? (width - len) : 0;
    int left = total / 2;
    int right = total - left;
    int p = 0;
    int i;

    for(i = 0; i < left && p < (int)dstsz - 1; i++) dst[p++] = ' ';
    for(i = 0; src[i] != '\0' && p < (int)dstsz - 1; i++) dst[p++] = src[i];
    for(i = 0; i < right && p < (int)dstsz - 1; i++) dst[p++] = ' ';
    dst[p] = '\0';
}

#define PROMPT_BTN_W    15      /* padded caption width (longest is 13) */

static vk_window_t*
build_prompt(scrshot_session_t *s)
{
    vk_popup_t      *popup;
    vk_box_t        *client;
    vk_box_t        *bar;
    vk_filler_t     *pad_top;
    vk_filler_t     *pad_bot;
    vk_label_t      *line;
    char            b_screen[32];
    char            b_window[32];
    char            b_cancel[32];
    int             W = 54;
    int             H = 9;

    /* pad all captions to equal width so the buttons are evenly spaced */
    center_pad(b_screen, sizeof(b_screen), "Entire screen", PROMPT_BTN_W);
    center_pad(b_window, sizeof(b_window), "Top window",    PROMPT_BTN_W);
    center_pad(b_cancel, sizeof(b_cancel), "Cancel",        PROMPT_BTN_W);

    popup = vk_popup_create(W, H, VK_BORDER_SINGLE,
        b_screen, b_window, b_cancel, NULL);
    vk_popup_set_title(popup, " Screen Capture ");
    vk_popup_set_border_colors(popup, COLOR_WHITE, COLOR_CYAN);
    vk_popup_set_border_attrs(popup, A_BOLD);
    vk_popup_set_colors(popup, COLOR_BLACK, COLOR_CYAN);
    vk_popup_set_button_colors(popup, COLOR_BLACK, COLOR_CYAN);
    vk_popup_set_button_attrs(popup, A_BOLD);

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(popup));
        vk_widget_set_state(VK_WIDGET(popup), st | VK_STATE_NORESIZE);
    }

    bar = vk_popup_get_button_bar(popup);
    if(bar != NULL)
    {
        /* equal-size slots so the equal-width buttons space evenly */
        vk_box_set_homogeneous(bar, true);
        vk_widget_set_colors(VK_WIDGET(bar), COLOR_BLACK, COLOR_CYAN);
        vk_widget_fill(VK_WIDGET(bar),
            ' ' | COLOR_PAIR(vdk_color_pair(COLOR_BLACK, COLOR_CYAN)));
    }

    client = vk_box_create(W - 2, H - 5, VK_BOX_VERTICAL, 3);
    vk_box_set_homogeneous(client, false);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_BLACK, COLOR_CYAN);

    pad_top = vk_filler_create();
    vk_widget_set_colors(VK_WIDGET(pad_top), COLOR_BLACK, COLOR_CYAN);
    vk_box_set_widget(client, 0, VK_WIDGET(pad_top));

    line = vk_label_create(W - 2);
    vk_label_set_justify(line, VK_JUSTIFY_CENTER);
    vk_label_set_text(line, "What do you want to capture?");
    vk_widget_set_colors(VK_WIDGET(line), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(line);
    vk_box_set_widget(client, 1, VK_WIDGET(line));

    pad_bot = vk_filler_create();
    vk_widget_set_colors(VK_WIDGET(pad_bot), COLOR_BLACK, COLOR_CYAN);
    vk_box_set_widget(client, 2, VK_WIDGET(pad_bot));

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(popup), session_kmio);
    vk_object_register_event(VK_OBJECT(popup), VWM_EVENT_ON_CLOSE,
        scrshot_on_close, NULL);

    s->prompt_popup = popup;
    s->prompt_btn = 0;
    s->client_box = NULL;

    update_prompt_focus(s);

    /*
        vk_popup_update() only re-draws the layout box by copying each
        child's composer; it does NOT recurse into the nested client box.
        So the client must be filled and updated itself first, otherwise
        its composer is the uninitialized (black) canvas and the label
        renders black-on-black.
    */
    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_BLACK, COLOR_CYAN)));
    vk_box_update(client);

    vk_popup_update(popup);

    return VK_WINDOW(popup);
}

static void
update_prompt_focus(scrshot_session_t *s)
{
    int count;
    int i;

    if(s->prompt_popup == NULL) return;

    count = vk_popup_get_button_count(s->prompt_popup);

    for(i = 0; i < count; i++)
    {
        vk_button_t *btn = vk_popup_get_button(s->prompt_popup, i);

        vk_button_release(btn);
        vk_widget_set_colors(VK_WIDGET(btn),
            (i == s->prompt_btn) ? COLOR_YELLOW : COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        vk_button_update(btn);
    }
}

static void
prompt_activate(scrshot_session_t *s, int btn)
{
    (void)s;

    if(btn == 2)
        close_session();
    else
        begin_save((btn == 0) ? TARGET_SCREEN : TARGET_WINDOW);
}

/* ── save window ────────────────────────────────────────────────────────── */

static void
update_save_focus(scrshot_session_t *s)
{
    vk_widget_t *bar_w;
    vk_widget_t *ok = NULL;
    vk_widget_t *cancel = NULL;

    if(s->fname == NULL || s->fd == NULL) return;

    if(s->save_focus == SF_FILENAME)
    {
        vk_widget_set_colors(VK_WIDGET(s->fname), COLOR_CYAN, COLOR_BLUE);
        vk_input_show_cursor(s->fname, true);
    }
    else
    {
        vk_widget_set_colors(VK_WIDGET(s->fname), COLOR_WHITE, COLOR_BLUE);
        vk_input_show_cursor(s->fname, false);
    }

    vk_input_update(s->fname);

    /*
        the OK/Cancel buttons live in the filedialog's button bar (slot 2);
        reach them through the public box getter so we can highlight the
        focused one (no need to pull in the filedialog struct header).
    */
    bar_w = vk_box_get_widget(VK_BOX(s->fd), 2);
    if(bar_w != NULL)
    {
        ok = vk_box_get_widget(VK_BOX(bar_w), 0);
        cancel = vk_box_get_widget(VK_BOX(bar_w), 1);
    }

    if(ok != NULL)
    {
        vk_button_release(VK_BUTTON(ok));
        vk_widget_set_colors(ok,
            (s->save_focus == SF_OK) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(ok, A_BOLD);
        vk_button_update(VK_BUTTON(ok));
    }

    if(cancel != NULL)
    {
        vk_button_release(VK_BUTTON(cancel));
        vk_widget_set_colors(cancel,
            (s->save_focus == SF_CANCEL) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(cancel, A_BOLD);
        vk_button_update(VK_BUTTON(cancel));
    }

    /* toggle the listbox highlight to reflect focus state */
    vk_listbox_set_focused(vk_filedialog_get_file_list(s->fd),
        s->save_focus == SF_FILEDIALOG);
}

static vk_window_t*
build_save(scrshot_session_t *s)
{
    vwm_t           *vwm = vwm_get_instance();
    vk_window_t     *win;
    vk_box_t        *box;
    vk_filedialog_t *fd;
    vk_input_t      *fname;
    char            defname[64];
    int             scr_w, scr_h;
    int             W = SAVE_W;
    int             H = SAVE_H;
    int             fd_h;
    time_t          now;
    struct tm       tmv;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
    if(W > scr_w - 2) W = scr_w - 2;
    if(H > scr_h - 2) H = scr_h - 2;
    if(W < 24) W = 24;
    if(H < 12) H = 12;

    /* the "Save as" filename input (3 rows) sits above the file browser */
    fd_h = (H - 2) - 3;
    if(fd_h < 6) fd_h = 6;

    win = make_window(W, H, " Save Screenshot ", COLOR_WHITE, COLOR_BLUE);

    box = vk_box_create(W - 2, H - 2, VK_BOX_VERTICAL, 2);
    vk_box_set_homogeneous(box, false);
    vk_widget_set_colors(VK_WIDGET(box), COLOR_WHITE, COLOR_BLUE);

    fname = vk_input_create(W - 2);
    vk_input_set_border_style(fname, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(fname), COLOR_WHITE, COLOR_BLUE);

    now = time(NULL);
    localtime_r(&now, &tmv);
    strftime(defname, sizeof(defname), "vwm-%Y%m%d-%H%M%S.png", &tmv);
    vk_input_set_text(fname, defname);
    vk_input_update(fname);
    vk_box_set_widget(box, 0, VK_WIDGET(fname));

    fd = vk_filedialog_create(W - 2, fd_h, VK_BORDER_SINGLE, false);
    vk_filedialog_set_colors(fd, COLOR_WHITE, COLOR_BLUE);
    /* grey active selection (matches the unfocused row), not red */
    vk_filedialog_set_highlight(fd, COLOR_BLACK, COLOR_WHITE);
    vk_listbox_set_unfocused(vk_filedialog_get_file_list(fd),
        COLOR_BLACK, COLOR_WHITE);
    vk_filedialog_set_button_colors(fd, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_button_attrs(fd, A_BOLD);
    vk_filedialog_set_wrap(fd, false);
    vk_widget_set_expand(VK_WIDGET(fd));
    vk_box_set_widget(box, 1, VK_WIDGET(fd));

    vk_window_set_child(win, VK_WIDGET(box));
    vk_object_set_kmio(VK_OBJECT(win), session_kmio);

    s->fd = fd;
    s->fname = fname;
    s->hint = NULL;
    s->client_box = box;
    s->save_focus = SF_FILENAME;

    update_save_focus(s);

    vk_filedialog_update(fd);
    vk_box_update(box);

    return win;
}

/* ── result window ──────────────────────────────────────────────────────── */

static vk_window_t*
build_result(const char *msg)
{
    vwm_t       *vwm = vwm_get_instance();
    vk_window_t *win;
    vk_box_t    *box;
    vk_label_t  *line;
    vk_label_t  *foot;
    int         scr_w, scr_h;
    int         W;
    int         H = 6;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
    (void)scr_h;

    W = (int)strlen(msg) + 4;
    if(W < 24) W = 24;
    if(W > scr_w - 2) W = scr_w - 2;

    win = make_window(W, H, " Screen Capture ", COLOR_WHITE, COLOR_CYAN);

    box = vk_box_create(W - 2, H - 2, VK_BOX_VERTICAL, 3);
    vk_box_set_homogeneous(box, false);
    vk_widget_set_colors(VK_WIDGET(box), COLOR_BLACK, COLOR_CYAN);

    line = vk_label_create(W - 2);
    vk_label_set_text(line, msg);
    vk_widget_set_colors(VK_WIDGET(line), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(line);
    vk_box_set_widget(box, 0, VK_WIDGET(line));

    foot = vk_label_create(W - 2);
    vk_label_set_text(foot, " Press any key to close.");
    vk_widget_set_colors(VK_WIDGET(foot), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(foot);
    vk_box_set_widget(box, 2, VK_WIDGET(foot));

    vk_window_set_child(win, VK_WIDGET(box));
    vk_object_set_kmio(VK_OBJECT(win), session_kmio);

    s_session->client_box = box;

    vk_box_update(box);

    return win;
}

/* ── window lifecycle ───────────────────────────────────────────────────── */

/* destroy a window we own without letting ON_CLOSE tear down the session */
static void
destroy_own_window(vk_window_t *win)
{
    vwm_t *vwm = vwm_get_instance();

    if(win == NULL) return;

    s_module_close = 1;
    if(s_session != NULL)
        vk_screen_detach_widget(vwm->screen, s_session->surface,
            VK_WIDGET(win));
    vk_widget_destroy(VK_WIDGET(win));
    s_module_close = 0;
}

/* fires when the window is closed by the WM ([X], Alt+w, …) rather than
   by us; the core destroys the window, so we only release the session */
static int
scrshot_on_close(vk_object_t *object, int event, void *anything)
{
    (void)object;
    (void)event;
    (void)anything;

    if(s_module_close) return 0;

    if(s_session != NULL)
    {
        vwm_get_instance()->tool_window = NULL;
        s_session->window = NULL;
        free(s_session);
        s_session = NULL;
    }

    return 0;
}

static void
swap_window(vk_window_t *new_win)
{
    scrshot_session_t   *s = s_session;
    vwm_t               *vwm = vwm_get_instance();

    if(s->window != NULL)
    {
        destroy_own_window(s->window);
        s->window = NULL;
    }

    s->window = new_win;
    if(new_win == NULL) return;

    center_window(new_win);
    vk_screen_attach_widget(vwm->screen, s->surface, VK_WIDGET(new_win));
    vwm->tool_window = new_win;
    vk_window_update(new_win);
    vk_screen_refresh(vwm->screen);
}

static void
close_session(void)
{
    scrshot_session_t   *s = s_session;
    vwm_t               *vwm;
    vk_widget_t         *new_top;

    if(s == NULL) return;

    vwm = vwm_get_instance();

    if(s->window != NULL)
    {
        destroy_own_window(s->window);
        s->window = NULL;
    }

    vwm->tool_window = NULL;

    free(s);
    s_session = NULL;

    new_top = vk_deck_get_top(vwm->deck);
    if(new_top != NULL)
    {
        vwm_panel_set_status(VWM_WINDOW_HELP);
        vk_window_update(VK_WINDOW(new_top));
    }
    else
    {
        vwm_panel_set_status("Press Alt ~ for Menu");
    }

    vk_screen_refresh(vwm->screen);
}

static void
begin_save(int target)
{
    scrshot_session_t   *s = s_session;
    vk_window_t         *win;

    s->target = target;
    s->state = ST_SAVE;

    win = build_save(s);
    swap_window(win);
    s->prompt_popup = NULL;     /* the prompt popup was destroyed by swap */

    vwm_panel_set_status(" [Tab] move  [Enter] activate  [Esc] cancel");
}

/* ── capture + write ────────────────────────────────────────────────────── */

static const char*
err_text(int code)
{
    switch(code)
    {
        case VWM_SHOT_ERR_FONT:  return "font load failed";
        case VWM_SHOT_ERR_GLYPH: return "glyph load failed";
        case VWM_SHOT_ERR_ALLOC: return "out of memory";
        case VWM_SHOT_ERR_PNG:   return "PNG write failed";
        default:                 return "unknown error";
    }
}

static void
do_capture_and_save(void)
{
    scrshot_session_t   *s = s_session;
    const char          *dir;
    const char          *name;
    char                fullpath[PATH_MAX];
    char                msg[PATH_MAX + 64];
    int                 code;
    int                 target;

    dir = vk_filedialog_get_path(s->fd);
    name = vk_input_get_text(s->fname);

    if(name == NULL || name[0] == '\0')
    {
        vwm_panel_set_status(" Enter a filename, then press Enter to save.");
        s->save_focus = SF_FILENAME;
        update_save_focus(s);
        refresh_session(s);
        return;
    }

    if(dir == NULL) dir = ".";

    if(strcmp(dir, "/") == 0)
        snprintf(fullpath, sizeof(fullpath), "/%s", name);
    else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, name);

    target = (s->target == TARGET_WINDOW) ? VWM_SHOT_TOP : VWM_SHOT_SCREEN;

    /* remove our dialog before capturing so it is not in the screenshot */
    if(s->window != NULL)
    {
        destroy_own_window(s->window);
        s->window = NULL;
        s->fd = NULL;
        s->fname = NULL;
        s->hint = NULL;
        s->client_box = NULL;
    }

    code = vwm_screenshot_save(target, fullpath);

    if(code == VWM_SHOT_OK)
        snprintf(msg, sizeof(msg), " Saved: %s", fullpath);
    else
        snprintf(msg, sizeof(msg), " Error: %s", err_text(code));

    s->state = ST_RESULT;
    swap_window(build_result(msg));
}

/* ── mouse ──────────────────────────────────────────────────────────────── */

static void
handle_mouse(MEVENT *me)
{
    scrshot_session_t   *s = s_session;
    int                 wx, wy, ww, wh;
    int                 ix, iy;
    mmask_t             bs;

    if(s == NULL || me == NULL || s->window == NULL) return;

    bs = me->bstate;

    vk_widget_get_position(VK_WIDGET(s->window), &wx, &wy);
    vk_widget_get_metrics(VK_WIDGET(s->window), &ww, &wh);

    ix = me->x - wx - 1;        /* interior column */
    iy = me->y - wy - 1;        /* interior row    */

    if(ix < 0 || ix >= ww - 2 || iy < 0 || iy >= wh - 2)
    {
        /* off the dialog -- ignore.  A press off the window no longer
           dismisses the screenshot tool; use Cancel or Esc instead. */
        return;
    }

    if(s->state == ST_RESULT)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            close_session();
        return;
    }

    if(s->state == ST_PROMPT)
    {
        int interior_w = ww - 2;
        int interior_h = wh - 2;

        /* the button bar occupies the bottom 3 interior rows */
        if(iy >= interior_h - 3 &&
            (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
        {
            int third = interior_w / 3;
            int btn;

            if(third < 1) third = 1;
            btn = ix / third;
            if(btn < 0) btn = 0;
            if(btn > 2) btn = 2;

            s->prompt_btn = btn;
            update_prompt_focus(s);
            refresh_session(s);
            prompt_activate(s, btn);
        }
        return;
    }

    /* ST_SAVE: "Save as" input (rows 0..2) on top, file browser below.
       Coordinates within the browser mirror the Load feature: a 3-row
       path strip, a list, then a 3-row OK/Cancel button bar. */
    {
        int             fname_h = 3;
        int             fd_top = fname_h;
        int             fd_h = (wh - 2) - fname_h;
        int             interior_w = ww - 2;
        int             rel;
        vk_listbox_t    *fl;

        /* "Save as" filename field */
        if(iy < fname_h)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                s->save_focus = SF_FILENAME;
                update_save_focus(s);
                refresh_session(s);
            }
            return;
        }

        rel = iy - fd_top;              /* row within the file dialog */
        if(rel < 0 || rel >= fd_h) return;

        s->save_focus = SF_FILEDIALOG;

        /* path-input strip at the top of the dialog */
        if(rel < 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                vk_object_push_keystroke(VK_OBJECT(s->fd), '/');
                update_save_focus(s);
                refresh_session(s);
            }
            return;
        }

        /* OK / Cancel button row at the bottom of the dialog */
        if(rel >= fd_h - 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                if(ix < interior_w / 2)
                    do_capture_and_save();
                else
                    close_session();
            }
            return;
        }

        /* file list */
        fl = vk_filedialog_get_file_list(s->fd);
        if(fl == NULL) return;

        if(bs & BUTTON4_PRESSED)
        {
            vk_listbox_set_prev(fl);
            refresh_session(s);
            return;
        }
        if(bs & BUTTON5_PRESSED)
        {
            vk_listbox_set_next(fl);
            refresh_session(s);
            return;
        }

        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            int             scroll = vk_listbox_get_scroll_pos(fl);
            /* rel - 3 skips the 3-row input strip; -1 more skips the
               sunken-relief frame's top border */
            int             clicked = scroll + (rel - 3 - 1);
            int             count = vk_listbox_get_item_count(fl);
            struct timespec now;
            bool            dbl = false;

            if(clicked < 0 || clicked >= count) return;

            clock_gettime(CLOCK_MONOTONIC, &now);
            if(clicked == s->last_click_item)
            {
                long ms = (now.tv_sec - s->last_click_time.tv_sec) * 1000
                    + (now.tv_nsec - s->last_click_time.tv_nsec) / 1000000;
                if(ms >= 0 && ms < 400) dbl = true;
            }
            s->last_click_time = now;
            s->last_click_item = clicked;

            vk_listbox_set_curr(fl, clicked);
            refresh_session(s);

            if(dbl)
            {
                const char  *sel = vk_filedialog_get_selected(s->fd);

                if(sel != NULL && sel[0] != '\0')
                {
                    int len = (int)strlen(sel);

                    if((len > 0 && sel[len - 1] == '/') ||
                        strcmp(sel, "..") == 0)
                    {
                        /* directory: descend into it */
                        vk_object_push_keystroke(VK_OBJECT(s->fd), KEY_CRLF);
                        refresh_session(s);
                    }
                    else
                    {
                        /* file: adopt its name, jump to the filename field */
                        vk_input_set_text(s->fname, sel);
                        s->save_focus = SF_FILENAME;
                        update_save_focus(s);
                        refresh_session(s);
                    }
                }
            }
        }
    }
}

/* ── keyboard ───────────────────────────────────────────────────────────── */

static int
session_kmio(vk_object_t *object, int32_t keystroke)
{
    scrshot_session_t *s = s_session;

    (void)object;

    if(s == NULL) return 0;

    if(keystroke == KEY_MOUSE)
    {
        handle_mouse(vk_kmio_get_mouse_event());
        return 0;
    }

    if(s->state == ST_RESULT)
    {
        close_session();
        return 0;
    }

    if(s->state == ST_PROMPT)
    {
        if(keystroke == 27)
        {
            close_session();
            return 0;
        }

        if(keystroke == '\t' || keystroke == KEY_RIGHT)
        {
            s->prompt_btn = (s->prompt_btn + 1) % 3;
            update_prompt_focus(s);
            refresh_session(s);
            return 0;
        }

        if(keystroke == KEY_LEFT)
        {
            s->prompt_btn = (s->prompt_btn + 2) % 3;
            update_prompt_focus(s);
            refresh_session(s);
            return 0;
        }

        if(keystroke == KEY_CRLF || keystroke == ' ')
        {
            prompt_activate(s, s->prompt_btn);
            return 0;
        }

        return 0;
    }

    /* ST_SAVE */
    if(keystroke == 27)
    {
        close_session();
        return 0;
    }

    if(keystroke == '\t')
    {
        s->save_focus = (s->save_focus + 1) % SF_COUNT;
        update_save_focus(s);
        refresh_session(s);
        return 0;
    }

    if(s->save_focus == SF_FILEDIALOG)
    {
        /* drive the file dialog (navigation, '/' for path entry) */
        vk_object_push_keystroke(VK_OBJECT(s->fd), keystroke);
        refresh_session(s);
        return 0;
    }

    if(s->save_focus == SF_OK)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            do_capture_and_save();
        return 0;
    }

    if(s->save_focus == SF_CANCEL)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            close_session();
        return 0;
    }

    /* SF_FILENAME: editing */
    switch(keystroke)
    {
        case KEY_CRLF:
            do_capture_and_save();
            return 0;

        case KEY_LEFT:
            vk_input_move_cursor(s->fname, -1);
            break;
        case KEY_RIGHT:
            vk_input_move_cursor(s->fname, 1);
            break;
        case KEY_HOME:
            vk_input_home(s->fname);
            break;
        case KEY_END:
            vk_input_end(s->fname);
            break;
        case KEY_BACKSPACE:
        case 127:
            vk_input_backspace(s->fname);
            break;
        case KEY_DC:
            vk_input_delete(s->fname);
            break;
        default:
            if(keystroke >= 32 && keystroke <= 126)
                vk_input_insert_char(s->fname, keystroke);
            break;
    }

    vk_input_update(s->fname);
    refresh_session(s);

    return 0;
}

void
vwm_screenshot_open(void)
{
    vwm_t       *vwm = vwm_get_instance();
    vk_window_t *win;

    /* only one capture session at a time */
    if(s_session != NULL) return;

    s_session = (scrshot_session_t *)calloc(1, sizeof(scrshot_session_t));
    if(s_session == NULL) return;

    s_session->state = ST_PROMPT;
    s_session->last_click_item = -1;

    win = build_prompt(s_session);
    s_session->window = win;

    center_window(win);

    /* a system tool is a surface overlay, not a deck-managed window */
    s_session->surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_attach_widget(vwm->screen, s_session->surface, VK_WIDGET(win));
    vwm->tool_window = win;

    vwm_panel_set_status(" [Tab] move  [Enter] choose  [Esc] cancel");

    refresh_session(s_session);
}
