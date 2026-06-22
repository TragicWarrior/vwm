#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <time.h>

#include <vdk.h>
#include <vkmio.h>
#include <ncursesw/curses.h>

#include "vwmprint.h"

#include "../../vwm.h"
#include "../../modules.h"
#include "../../private.h"
#include "../../panel.h"
#include "../../winman.h"

#define PICK_W      64
#define PICK_H      22

#define PR_BTN_W    9       /* padded width for the Print/Cancel captions */

/* session states */
enum
{
    ST_PICK = 0,    /* browsing for a file to print */
    ST_PRINTER,     /* choosing a printer            */
    ST_RESULT       /* showing the outcome           */
};

/* focus stops in the printer window */
enum
{
    PR_LIST = 0,
    PR_PRINT,
    PR_CANCEL,
    PR_COUNT
};

/* focus stops in the file-picker window */
enum
{
    PK_FILEDIALOG = 0,
    PK_OKAY,
    PK_CANCEL,
    PK_COUNT
};

typedef struct
{
    int             state;
    int             surface;        /* surface the overlay is attached to  */

    vk_window_t     *window;        /* the single surface-overlay window   */
    vk_box_t        *client_box;    /* nested layout box (printer/result)  */

    /* pick state */
    vk_filedialog_t *fd;
    int             pick_focus;

    /* chosen file (absolute path) */
    char            file_path[PATH_MAX];

    /* printer state */
    vk_popup_t      *printer_popup;
    vk_frame_t      *printer_frame;
    vk_listbox_t    *printer_list;
    char            **printers;
    int             printer_count;
    int             printer_focus;

    /* double-click tracking for the lists */
    struct timespec last_click_time;
    int             last_click_item;
}
print_session_t;

static print_session_t  *s_session = NULL;

/* set while we destroy our own window so ON_CLOSE doesn't double-free */
static int              s_module_close = 0;

/* forward declarations */
static int          session_kmio(vk_object_t *object, int32_t keystroke);
static int          print_on_close(vk_object_t *object, int event,
                        void *anything);
static void         handle_mouse(MEVENT *me);
static vk_window_t* build_pick(print_session_t *s);
static vk_window_t* build_printers(print_session_t *s);
static vk_window_t* build_result(const char *msg);
static void         swap_window(vk_window_t *new_win);
static void         close_session(void);
static void         refresh_session(print_session_t *s);
static void         choose_file(print_session_t *s, const char *sel);
static void         begin_printers(print_session_t *s);
static void         do_print(print_session_t *s);
static void         update_printer_focus(print_session_t *s);
static void         update_pick_focus(print_session_t *s);
static void         activate_selection(print_session_t *s);

/* ── helpers ─────────────────────────────────────────────────── */

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

/* center src within `width` columns, padding with spaces */
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

static void
refresh_session(print_session_t *s)
{
    vwm_t *vwm = vwm_get_instance();

    if(s == NULL) return;

    /*
        nested children only get blitted by the parent update; re-render
        them into their composers first or interactions won't show.
    */
    if(s->printer_popup != NULL)
    {
        if(s->printer_list != NULL) vk_listbox_update(s->printer_list);
        if(s->printer_frame != NULL) vk_frame_update(s->printer_frame);
        if(s->client_box != NULL) vk_box_update(s->client_box);
        vk_popup_update(s->printer_popup);
        vk_screen_refresh(vwm->screen);
        return;
    }

    if(s->state == ST_PICK && s->fd != NULL)
        vk_filedialog_update(s->fd);
    else if(s->client_box != NULL)
        vk_box_update(s->client_box);

    if(s->window != NULL)
        vk_window_update(s->window);

    vk_screen_refresh(vwm->screen);
}

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

    vk_object_register_event(VK_OBJECT(win), VWM_EVENT_ON_CLOSE,
        print_on_close, NULL);

    return win;
}

/* ── file picker (blue file-dialog theme) ────────────────────── */

static vk_window_t*
build_pick(print_session_t *s)
{
    vwm_t           *vwm = vwm_get_instance();
    vk_window_t     *win;
    vk_filedialog_t *fd;
    int             scr_w, scr_h;
    int             W = PICK_W;
    int             H = PICK_H;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);
    if(W > scr_w - 2) W = scr_w - 2;
    if(H > scr_h - 2) H = scr_h - 2;
    if(W < 24) W = 24;
    if(H < 12) H = 12;

    win = make_window(W, H, " Print File ", COLOR_WHITE, COLOR_BLUE);

    fd = vk_filedialog_create(W - 2, H - 2, VK_BORDER_SINGLE, false);
    vk_filedialog_set_colors(fd, COLOR_WHITE, COLOR_BLUE);
    /* grey active selection (matches the unfocused row), not red */
    vk_filedialog_set_highlight(fd, COLOR_BLACK, COLOR_WHITE);
    vk_listbox_set_unfocused(vk_filedialog_get_file_list(fd),
        COLOR_BLACK, COLOR_WHITE);
    vk_filedialog_set_button_colors(fd, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_button_attrs(fd, A_BOLD);
    vk_filedialog_set_wrap(fd, false);
    vk_filedialog_set_filter(fd, "pdf,md,txt");
    vk_widget_set_expand(VK_WIDGET(fd));

    vk_window_set_child(win, VK_WIDGET(fd));
    vk_object_set_kmio(VK_OBJECT(win), session_kmio);

    s->fd = fd;
    s->printer_popup = NULL;
    s->printer_frame = NULL;
    s->printer_list = NULL;
    s->client_box = NULL;

    vk_filedialog_update(fd);

    return win;
}

/* ── file picker focus (Tab cycles: filedialog → Okay → Cancel) ── */

static void
update_pick_focus(print_session_t *s)
{
    vk_widget_t *bar_w;
    vk_widget_t *ok = NULL;
    vk_widget_t *cancel = NULL;

    if(s->fd == NULL) return;

    /*
        the Okay/Cancel buttons live in the filedialog's button bar
        (slot 2 of the filedialog box).  Reach them via the public box
        getter so we don't have to pull in vk_filedialog.h.
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
            (s->pick_focus == PK_OKAY) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(ok, A_BOLD);
        vk_button_update(VK_BUTTON(ok));
    }

    if(cancel != NULL)
    {
        vk_button_release(VK_BUTTON(cancel));
        vk_widget_set_colors(cancel,
            (s->pick_focus == PK_CANCEL) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(cancel, A_BOLD);
        vk_button_update(VK_BUTTON(cancel));
    }

    /* toggle the listbox highlight to reflect focus state */
    vk_listbox_set_focused(vk_filedialog_get_file_list(s->fd),
        s->pick_focus == PK_FILEDIALOG);
}

/* ── printer list (system cyan theme, Print/Cancel buttons) ──── */

static void
update_printer_focus(print_session_t *s)
{
    int count;
    int i;

    if(s->printer_popup == NULL) return;

    count = vk_popup_get_button_count(s->printer_popup);

    for(i = 0; i < count; i++)
    {
        vk_button_t *btn = vk_popup_get_button(s->printer_popup, i);
        int          focused;

        if(btn == NULL) continue;

        focused = ((i == 0 && s->printer_focus == PR_PRINT) ||
                   (i == 1 && s->printer_focus == PR_CANCEL));

        vk_button_release(btn);
        vk_widget_set_colors(VK_WIDGET(btn),
            focused ? COLOR_YELLOW : COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        vk_button_update(btn);
    }

    /* the list frame turns yellow when the list is the active tab stop */
    if(s->printer_frame != NULL)
    {
        vk_frame_set_border_colors(s->printer_frame,
            (s->printer_focus == PR_LIST) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_CYAN);
        vk_frame_set_border_attrs(s->printer_frame, A_BOLD);
        vk_frame_update(s->printer_frame);
    }

    if(s->printer_list != NULL)
    {
        vk_listbox_set_focused(s->printer_list,
            s->printer_focus == PR_LIST);
        vk_listbox_update(s->printer_list);
    }
}

static vk_window_t*
build_printers(print_session_t *s)
{
    vwm_t           *vwm = vwm_get_instance();
    vk_popup_t      *popup;
    vk_box_t        *client;
    vk_box_t        *bar;
    vk_frame_t      *frame;
    vk_listbox_t    *lb;
    char            b_print[16];
    char            b_cancel[16];
    int             scr_w, scr_h;
    int             W, H, i, maxlen = 0;

    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    for(i = 0; i < s->printer_count; i++)
    {
        int len = (int)strlen(s->printers[i]);
        if(len > maxlen) maxlen = len;
    }

    W = maxlen + 6;
    if(W < 32) W = 32;
    if(W > scr_w - 2) W = scr_w - 2;

    /* interior = framed list + 3-row button bar; the window adds 2 for its
       border, the frame adds 2, the bar takes 3 -> list rows = H - 7 */
    H = s->printer_count + 7;
    if(H < 12) H = 12;
    if(H > scr_h - 2) H = scr_h - 2;

    center_pad(b_print, sizeof(b_print), "Print", PR_BTN_W);
    center_pad(b_cancel, sizeof(b_cancel), "Cancel", PR_BTN_W);

    popup = vk_popup_create(W, H, VK_BORDER_SINGLE, b_print, b_cancel, NULL);
    vk_popup_set_title(popup, " Select Printer ");
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
        vk_box_set_homogeneous(bar, true);
        vk_widget_set_colors(VK_WIDGET(bar), COLOR_BLACK, COLOR_CYAN);
        vk_widget_fill(VK_WIDGET(bar),
            ' ' | COLOR_PAIR(vdk_color_pair(COLOR_BLACK, COLOR_CYAN)));
    }

    client = vk_box_create(W - 2, H - 5, VK_BOX_VERTICAL, 0);
    vk_box_set_homogeneous(client, false);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_BLACK, COLOR_CYAN);

    frame = vk_frame_create(W - 2, H - 5);
    vk_frame_set_border_style(frame, VK_BORDER_SINGLE | VK_RELIEF_SUNKEN);
    vk_frame_set_border_colors(frame, COLOR_YELLOW, COLOR_CYAN);
    vk_frame_set_border_attrs(frame, A_BOLD);
    vk_widget_set_colors(VK_WIDGET(frame), COLOR_BLACK, COLOR_CYAN);

    lb = vk_listbox_create(W - 4, H - 7);
    vk_widget_set_colors(VK_WIDGET(lb), COLOR_BLACK, COLOR_CYAN);
    vk_listbox_set_highlight(lb, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_unfocused(lb, COLOR_BLACK, COLOR_WHITE);
    vk_listbox_set_highlight_attrs(lb, A_BOLD);
    vk_listbox_set_wrap(lb, false);

    for(i = 0; i < s->printer_count; i++)
        vk_listbox_add_item(lb, s->printers[i], NULL, NULL);

    vk_listbox_set_curr(lb, 0);
    vk_widget_set_expand(VK_WIDGET(lb));
    vk_frame_set_child(frame, VK_WIDGET(lb));

    vk_widget_set_expand(VK_WIDGET(frame));
    vk_box_set_widget(client, 0, VK_WIDGET(frame));

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(popup), session_kmio);
    vk_object_register_event(VK_OBJECT(popup), VWM_EVENT_ON_CLOSE,
        print_on_close, NULL);

    s->printer_popup = popup;
    s->printer_frame = frame;
    s->printer_list = lb;
    s->client_box = client;
    s->printer_focus = PR_LIST;
    s->fd = NULL;

    update_printer_focus(s);

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_BLACK, COLOR_CYAN)));
    vk_listbox_update(lb);
    vk_frame_update(frame);
    vk_box_update(client);
    vk_popup_update(popup);

    return VK_WINDOW(popup);
}

/* ── result window (system cyan theme) ───────────────────────── */

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
    if(W < 28) W = 28;
    if(W > scr_w - 2) W = scr_w - 2;

    win = make_window(W, H, " Print File ", COLOR_WHITE, COLOR_CYAN);

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
    s_session->fd = NULL;
    s_session->printer_popup = NULL;
    s_session->printer_frame = NULL;
    s_session->printer_list = NULL;

    vk_box_update(box);

    return win;
}

/* ── window lifecycle ────────────────────────────────────────── */

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

static int
print_on_close(vk_object_t *object, int event, void *anything)
{
    (void)object;
    (void)event;
    (void)anything;

    if(s_module_close) return 0;

    if(s_session != NULL)
    {
        vwm_get_instance()->tool_window = NULL;
        s_session->window = NULL;
        if(s_session->printers != NULL)
            vwmprint_free_printers(s_session->printers,
                s_session->printer_count);
        free(s_session);
        s_session = NULL;
    }

    return 0;
}

static void
swap_window(vk_window_t *new_win)
{
    print_session_t *s = s_session;
    vwm_t           *vwm = vwm_get_instance();

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
    print_session_t *s = s_session;
    vwm_t           *vwm;
    vk_widget_t     *new_top;

    if(s == NULL) return;

    vwm = vwm_get_instance();

    if(s->window != NULL)
    {
        destroy_own_window(s->window);
        s->window = NULL;
    }

    vwm->tool_window = NULL;

    if(s->printers != NULL)
    {
        vwmprint_free_printers(s->printers, s->printer_count);
        s->printers = NULL;
    }

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

/* ── actions ─────────────────────────────────────────────────── */

static void
choose_file(print_session_t *s, const char *sel)
{
    const char  *dir;
    const char  *dot;

    if(sel == NULL || sel[0] == '\0') return;

    dir = vk_filedialog_get_path(s->fd);
    if(dir == NULL) dir = "/";

    if(strcmp(dir, "/") == 0)
        snprintf(s->file_path, sizeof(s->file_path), "/%s", sel);
    else
        snprintf(s->file_path, sizeof(s->file_path), "%s/%s", dir, sel);

    /* only the documented file types */
    dot = strrchr(sel, '.');
    if(dot == NULL ||
        (strcasecmp(dot, ".txt") != 0 &&
         strcasecmp(dot, ".md")  != 0 &&
         strcasecmp(dot, ".pdf") != 0))
    {
        vwm_panel_set_status(" Select a .txt, .md, or .pdf file.");
        return;
    }

    begin_printers(s);
}

static void
begin_printers(print_session_t *s)
{
    char    **names = NULL;
    int     count;

    count = vwmprint_list_printers(&names);

    if(count <= 0)
    {
        s->state = ST_RESULT;
        swap_window(build_result("No CUPS printers are configured."));
        return;
    }

    s->printers = names;
    s->printer_count = count;
    s->last_click_item = -1;
    s->state = ST_PRINTER;

    swap_window(build_printers(s));
    refresh_session(s);

    vwm_panel_set_status(" [Up/Down] choose  [Tab] move  [Enter] print  [Esc] cancel");
}

static void
do_print(print_session_t *s)
{
    char        msg[256];
    const char  *printer;
    int         idx;

    if(s->printer_list == NULL) return;

    idx = vk_listbox_get_curr(s->printer_list);
    if(idx < 0 || idx >= s->printer_count) return;

    printer = s->printers[idx];

    vwmprint_print(printer, s->file_path, msg, sizeof(msg));

    s->state = ST_RESULT;
    swap_window(build_result(msg));
}

/* select the highlighted file dialog entry: descend dirs, choose files */
static void
activate_selection(print_session_t *s)
{
    const char  *sel;
    size_t      len;

    sel = vk_filedialog_get_selected(s->fd);
    if(sel == NULL || sel[0] == '\0') return;

    len = strlen(sel);

    if((len > 0 && sel[len - 1] == '/') || strcmp(sel, "..") == 0)
    {
        vk_object_push_keystroke(VK_OBJECT(s->fd), KEY_CRLF);
        refresh_session(s);
    }
    else
    {
        choose_file(s, sel);
    }
}

/* ── mouse ───────────────────────────────────────────────────── */

static bool
double_click(print_session_t *s, int item)
{
    struct timespec now;
    bool            dbl = false;

    clock_gettime(CLOCK_MONOTONIC, &now);

    if(item == s->last_click_item)
    {
        long ms = (now.tv_sec - s->last_click_time.tv_sec) * 1000
            + (now.tv_nsec - s->last_click_time.tv_nsec) / 1000000;
        if(ms >= 0 && ms < 400) dbl = true;
    }

    s->last_click_time = now;
    s->last_click_item = item;

    return dbl;
}

static void
handle_mouse(MEVENT *me)
{
    print_session_t *s = s_session;
    int             wx, wy, ww, wh, ix, iy;
    mmask_t         bs;

    if(s == NULL || me == NULL || s->window == NULL) return;

    bs = me->bstate;

    vk_widget_get_position(VK_WIDGET(s->window), &wx, &wy);
    vk_widget_get_metrics(VK_WIDGET(s->window), &ww, &wh);

    ix = me->x - wx - 1;
    iy = me->y - wy - 1;

    if(ix < 0 || ix >= ww - 2 || iy < 0 || iy >= wh - 2)
    {
        /* off the dialog -- ignore.  A press off the window no longer
           dismisses the print tool; use Cancel or Esc instead. */
        return;
    }

    if(s->state == ST_RESULT)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)) close_session();
        return;
    }

    if(s->state == ST_PRINTER)
    {
        int             interior_w = ww - 2;
        int             interior_h = wh - 2;
        vk_listbox_t    *lb = s->printer_list;

        if(lb == NULL) return;

        /* Print / Cancel bar across the bottom 3 interior rows */
        if(iy >= interior_h - 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                if(ix < interior_w / 2)
                {
                    s->printer_focus = PR_PRINT;
                    update_printer_focus(s);
                    refresh_session(s);
                    do_print(s);
                }
                else
                {
                    close_session();
                }
            }
            return;
        }

        /* printer list (client area) */
        if(bs & BUTTON4_PRESSED) { vk_listbox_set_prev(lb); refresh_session(s); return; }
        if(bs & BUTTON5_PRESSED) { vk_listbox_set_next(lb); refresh_session(s); return; }

        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            int scroll = vk_listbox_get_scroll_pos(lb);
            int clicked = scroll + iy - 1;   /* -1 for the frame's top border */
            int count = vk_listbox_get_item_count(lb);

            if(clicked < 0 || clicked >= count) return;

            s->printer_focus = PR_LIST;
            vk_listbox_set_curr(lb, clicked);
            update_printer_focus(s);
            refresh_session(s);

            if(double_click(s, clicked)) do_print(s);
        }
        return;
    }

    /* ST_PICK: filedialog fills the interior -- path strip (rows 0-2),
       file list, then an OK/Cancel bar in the bottom 3 rows. */
    {
        int             fd_h = wh - 2;
        int             interior_w = ww - 2;
        vk_listbox_t    *fl;

        if(iy < 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                vk_object_push_keystroke(VK_OBJECT(s->fd), '/');
                refresh_session(s);
            }
            return;
        }

        if(iy >= fd_h - 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                if(ix < interior_w / 2)
                    activate_selection(s);
                else
                    close_session();
            }
            return;
        }

        fl = vk_filedialog_get_file_list(s->fd);
        if(fl == NULL) return;

        if(bs & BUTTON4_PRESSED) { vk_listbox_set_prev(fl); refresh_session(s); return; }
        if(bs & BUTTON5_PRESSED) { vk_listbox_set_next(fl); refresh_session(s); return; }

        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            int scroll = vk_listbox_get_scroll_pos(fl);
            /* iy - 3 skips the 3-row input strip; -1 more skips the
               sunken-relief frame's top border */
            int clicked = scroll + (iy - 3 - 1);
            int count = vk_listbox_get_item_count(fl);

            if(clicked < 0 || clicked >= count) return;

            vk_listbox_set_curr(fl, clicked);
            refresh_session(s);

            if(double_click(s, clicked)) activate_selection(s);
        }
    }
}

/* ── keyboard ────────────────────────────────────────────────── */

static int
session_kmio(vk_object_t *object, int32_t keystroke)
{
    print_session_t *s = s_session;

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

    if(keystroke == 27)
    {
        close_session();
        return 0;
    }

    if(s->state == ST_PICK)
    {
        /* Tab (and arrow keys) cycles focus: filedialog -> Okay -> Cancel */
        if(keystroke == '\t')
        {
            s->pick_focus = (s->pick_focus + 1) % PK_COUNT;
            update_pick_focus(s);
            refresh_session(s);
            return 0;
        }

        if(s->pick_focus == PK_OKAY)
        {
            if(keystroke == KEY_CRLF || keystroke == ' ')
                activate_selection(s);
            return 0;
        }

        if(s->pick_focus == PK_CANCEL)
        {
            if(keystroke == KEY_CRLF || keystroke == ' ')
                close_session();
            return 0;
        }

        /* PK_FILEDIALOG: forward to the dialog (existing behavior) */
        if(keystroke == KEY_CRLF)
        {
            activate_selection(s);
            return 0;
        }

        vk_object_push_keystroke(VK_OBJECT(s->fd), keystroke);
        refresh_session(s);
        return 0;
    }

    if(s->state == ST_PRINTER)
    {
        if(keystroke == KEY_UP)
        {
            vk_listbox_set_prev(s->printer_list);
            refresh_session(s);
            return 0;
        }
        if(keystroke == KEY_DOWN)
        {
            vk_listbox_set_next(s->printer_list);
            refresh_session(s);
            return 0;
        }
        if(keystroke == '\t' || keystroke == KEY_RIGHT)
        {
            s->printer_focus = (s->printer_focus + 1) % PR_COUNT;
            update_printer_focus(s);
            refresh_session(s);
            return 0;
        }
        if(keystroke == KEY_LEFT)
        {
            s->printer_focus = (s->printer_focus + PR_COUNT - 1) % PR_COUNT;
            update_printer_focus(s);
            refresh_session(s);
            return 0;
        }
        if(keystroke == KEY_CRLF || keystroke == ' ')
        {
            if(s->printer_focus == PR_CANCEL)
                close_session();
            else
                do_print(s);
            return 0;
        }
        return 0;
    }

    return 0;
}

/* ── module entry ────────────────────────────────────────────── */

static vk_window_t*
vwmprint_main(vwm_module_t *mod)
{
    vwm_t       *vwm = vwm_get_instance();
    vk_window_t *win;

    (void)mod;

    /* only one print session at a time */
    if(s_session != NULL) return NULL;

    s_session = (print_session_t *)calloc(1, sizeof(print_session_t));
    if(s_session == NULL) return NULL;

    s_session->state = ST_PICK;
    s_session->last_click_item = -1;
    s_session->pick_focus = PK_FILEDIALOG;

    win = build_pick(s_session);
    s_session->window = win;

    /* paint initial button colors so they read as inactive */
    update_pick_focus(s_session);

    center_window(win);

    /* a system tool is a surface overlay, not a deck-managed window */
    s_session->surface = vk_screen_get_active_surface(vwm->screen);
    vk_screen_attach_widget(vwm->screen, s_session->surface, VK_WIDGET(win));
    vwm->tool_window = win;

    vwm_panel_set_status(" [Enter] choose  [Esc] cancel");

    refresh_session(s_session);

    /* return NULL so the launcher does not add us to the deck */
    return NULL;
}

static int
vwmprint_configure(vwm_module_t *mod, va_list *argp)
{
    (void)mod;
    (void)argp;
    return 0;
}

int
vwm_mod_init(const char *modpath)
{
    vwm_module_t    *mod;

    (void)modpath;

    mod = (vwm_module_t *)calloc(1, sizeof(vwm_module_t));
    if(mod == NULL) return -1;

    mod->main = vwmprint_main;
    mod->clone = vwm_module_simple_clone;
    mod->configure = vwmprint_configure;

    vwm_module_set_name(mod, "print-file");
    vwm_module_set_title(mod, "Print File");
    vwm_module_set_type(mod, VWM_MOD_TYPE_TOOL);

    /* built-in tool: hidden from the Apps menu, launched by name */
    vwm_module_set_zone(mod, MODULE_ZONE_CORE);

    vwm_module_add(mod);

    return 0;
}
