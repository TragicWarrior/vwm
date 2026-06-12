#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <vdk.h>
#include <vkmio.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"

#include "../../vwm.h"
#include "../../private.h"
#include "../../panel.h"
#include "../../poll_input_thd.h"
#include "../../winman.h"

#define KEY_ENTER_CR    13
#define KEY_ENTER_LF    10

#define KEY_PASTE       0x561B

static int      key_alt_pgup;
static int      key_alt_pgdn;
static int      key_sel_up;
static int      key_sel_dn;
static int      key_sel_left;
static int      key_sel_right;
static int      key_sel_home;
static int      key_sel_end;

static char     *clipboard = NULL;
static size_t   clipboard_len = 0;

void
vwmterm_init_keycodes(void)
{
    key_alt_pgup  = key_defined("\033[5;3~");
    key_alt_pgdn  = key_defined("\033[6;3~");
    key_sel_up    = key_defined("\033[1;4A");
    key_sel_dn    = key_defined("\033[1;4B");
    key_sel_left  = key_defined("\033[1;4D");
    key_sel_right = key_defined("\033[1;4C");
    key_sel_home  = key_defined("\033[1;4H");
    key_sel_end   = key_defined("\033[1;4F");
}

/*
    OSC 52 base64-encodes the selection and asks the *outer* terminal
    emulator to write it to the host clipboard.  Works through any
    terminal that honors OSC 52 -- xterm (with allowWindowOps),
    kitty, foot, alacritty, wezterm, iTerm2, and tmux/screen with
    set-clipboard on.  On terminals that don't recognize the sequence
    (including the bare Linux console) it is silently consumed by the
    OSC parser, so emitting it is harmless.  No extra library or
    process is needed -- the bytes go straight out stdout, which is
    the same fd ncurses already drives.
*/

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
vwmterm_osc52_copy(const char *buf, size_t len)
{
    char            *enc;
    size_t          enc_len;
    size_t          i, j;
    unsigned char   a, b, c;

    if(buf == NULL || len == 0) return;

    enc_len = 4 * ((len + 2) / 3);
    enc = (char *)malloc(enc_len + 1);
    if(enc == NULL) return;

    j = 0;
    for(i = 0; i + 3 <= len; i += 3)
    {
        a = (unsigned char)buf[i];
        b = (unsigned char)buf[i + 1];
        c = (unsigned char)buf[i + 2];
        enc[j++] = b64_alphabet[a >> 2];
        enc[j++] = b64_alphabet[((a & 0x03) << 4) | (b >> 4)];
        enc[j++] = b64_alphabet[((b & 0x0f) << 2) | (c >> 6)];
        enc[j++] = b64_alphabet[c & 0x3f];
    }
    if(i < len)
    {
        a = (unsigned char)buf[i];
        b = (i + 1 < len) ? (unsigned char)buf[i + 1] : 0;
        enc[j++] = b64_alphabet[a >> 2];
        enc[j++] = b64_alphabet[((a & 0x03) << 4) | (b >> 4)];
        enc[j++] = (i + 1 < len)
            ? b64_alphabet[(b & 0x0f) << 2]
            : '=';
        enc[j++] = '=';
    }
    enc[j] = '\0';

    fputs("\033]52;c;", stdout);
    fputs(enc, stdout);
    fputs("\033\\", stdout);
    fflush(stdout);

    free(enc);
}

/*
    Pipe the selection through xclip(1) to claim the X CLIPBOARD
    selection.  xclip reads stdin, claims ownership, then forks itself
    into the background to service paste requests -- so the foreground
    shell child exits at EOF and pclose() returns immediately without
    reaping the long-lived xclip.  stderr is silenced so a missing
    xclip binary ("sh: xclip: not found") doesn't smear the ncurses
    screen; the failure is a silent no-op.
*/
static void
vwmterm_xclip_copy(const char *buf, size_t len)
{
    FILE *fp;

    if(buf == NULL || len == 0) return;

    fp = popen("xclip -selection clipboard 2>/dev/null", "w");
    if(fp == NULL) return;

    fwrite(buf, 1, len, fp);
    pclose(fp);
}

static void
vwmterm_highlight_cell(WINDOW *win, int row, int col)
{
    cchar_t     cc;
    wchar_t     wch[CCHARW_MAX];
    attr_t      attrs;
    short       pair;

    if(mvwin_wch(win, row, col, &cc) == ERR) return;
    getcchar(&cc, wch, &attrs, &pair, NULL);
    attrs ^= A_REVERSE;
    setcchar(&cc, wch, attrs, pair, NULL);
    mvwadd_wch(win, row, col, &cc);
}

static void
vwmterm_render_selection(vwmterm_data_t *vwmterm_data)
{
    vwm_t       *vwm;
    vterm_t     *vterm;
    WINDOW      *win;
    int         width, height;
    int         r1, c1, r2, c2;

    vwm = vwm_get_instance();
    vterm = vwmterm_data->vterm;
    win = vterm_wnd_get(vterm);
    vterm_wnd_size(vterm, &width, &height);

    vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);

    if(vwmterm_data->sel_anchor_row < vwmterm_data->sel_end_row ||
       (vwmterm_data->sel_anchor_row == vwmterm_data->sel_end_row &&
        vwmterm_data->sel_anchor_col <= vwmterm_data->sel_end_col))
    {
        r1 = vwmterm_data->sel_anchor_row;
        c1 = vwmterm_data->sel_anchor_col;
        r2 = vwmterm_data->sel_end_row;
        c2 = vwmterm_data->sel_end_col;
    }
    else
    {
        r1 = vwmterm_data->sel_end_row;
        c1 = vwmterm_data->sel_end_col;
        r2 = vwmterm_data->sel_anchor_row;
        c2 = vwmterm_data->sel_anchor_col;
    }

    for(int r = r1; r <= r2; r++)
    {
        int cs = (r == r1) ? c1 : 0;
        int ce = (r == r2) ? c2 : width - 1;

        for(int c = cs; c <= ce; c++)
            vwmterm_highlight_cell(win, r, c);
    }

    vk_window_update(vwmterm_data->window);
    vk_screen_refresh(vwm->screen);
}

static void
vwmterm_copy_selection(vwmterm_data_t *vwmterm_data)
{
    vterm_t         *vterm;
    vterm_cell_t    **cells;
    int             rows, cols;
    int             r1, c1, r2, c2;
    char            *buf;
    size_t          buf_sz;
    size_t          pos;
    char            mb[MB_LEN_MAX];
    int             len;

    vterm = vwmterm_data->vterm;
    cells = vterm_copy_buffer(vterm, &rows, &cols);
    if(cells == NULL) return;

    if(vwmterm_data->sel_anchor_row < vwmterm_data->sel_end_row ||
       (vwmterm_data->sel_anchor_row == vwmterm_data->sel_end_row &&
        vwmterm_data->sel_anchor_col <= vwmterm_data->sel_end_col))
    {
        r1 = vwmterm_data->sel_anchor_row;
        c1 = vwmterm_data->sel_anchor_col;
        r2 = vwmterm_data->sel_end_row;
        c2 = vwmterm_data->sel_end_col;
    }
    else
    {
        r1 = vwmterm_data->sel_end_row;
        c1 = vwmterm_data->sel_end_col;
        r2 = vwmterm_data->sel_anchor_row;
        c2 = vwmterm_data->sel_anchor_col;
    }

    if(r1 < 0) r1 = 0;
    if(r2 >= rows) r2 = rows - 1;
    if(c1 < 0) c1 = 0;
    if(c2 >= cols) c2 = cols - 1;

    buf_sz = (size_t)(r2 - r1 + 1) * (cols * MB_LEN_MAX + 1);
    buf = (char *)calloc(1, buf_sz);
    pos = 0;

    for(int r = r1; r <= r2; r++)
    {
        int col_start = (r == r1) ? c1 : 0;
        int col_end = (r == r2) ? c2 : cols - 1;

        for(int c = col_start; c <= col_end; c++)
        {
            wchar_t wch = cells[r][c].wch[0];
            if(wch == 0) wch = L' ';

            len = wctomb(mb, wch);
            if(len > 0)
            {
                memcpy(&buf[pos], mb, len);
                pos += len;
            }
        }

        while(pos > 0 && buf[pos - 1] == ' ') pos--;

        if(r < r2)
            buf[pos++] = '\n';
    }

    buf[pos] = '\0';

    if(clipboard != NULL) free(clipboard);
    clipboard = buf;
    clipboard_len = pos;

    {
        vwm_t   *vwm = vwm_get_instance();
        int     mode = (vwm != NULL)
            ? vwm->clipboard_mode
            : VWM_CLIPBOARD_NEVER;

        if(mode == VWM_CLIPBOARD_OSC52 || mode == VWM_CLIPBOARD_BOTH)
            vwmterm_osc52_copy(clipboard, clipboard_len);
        if(mode == VWM_CLIPBOARD_XCLIP || mode == VWM_CLIPBOARD_BOTH)
            vwmterm_xclip_copy(clipboard, clipboard_len);
    }

    for(int r = 0; r < rows; r++) free(cells[r]);
    free(cells);
}

static void
vwmterm_enter_selection(vwmterm_data_t *vwmterm_data)
{
    vterm_t *vterm = vwmterm_data->vterm;
    int     col, row;

    vterm_get_cursor_position(vterm, &col, &row);

    vwmterm_data->frozen = 1;
    vwmterm_data->sel_anchor_row = row;
    vwmterm_data->sel_anchor_col = col;
    vwmterm_data->sel_end_row = row;
    vwmterm_data->sel_end_col = col;

    vk_window_set_title(vwmterm_data->window,
        " SELECT (Enter=Copy, Esc=Cancel) ");

    vwm_panel_set_status(
        "SELECT MODE: Arrow keys to select, Enter to copy, Esc to cancel");
}

static void
vwmterm_exit_selection(vwmterm_data_t *vwmterm_data)
{
    vwm_t   *vwm;
    vterm_t *vterm = vwmterm_data->vterm;

    vwm = vwm_get_instance();

    vwmterm_data->frozen = 0;

    vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);

    vk_window_set_title(vwmterm_data->window, vwmterm_data->title);

    vwm_panel_set_status(VWM_WINDOW_HELP);

    vk_window_update(vwmterm_data->window);
    vk_screen_refresh(vwm->screen);
}

static int
vwmterm_write_mouse(vterm_t *vterm, vk_window_t *window, MEVENT *me)
{
    int win_x, win_y;
    MEVENT adjusted = *me;

    vk_widget_get_position(VK_WIDGET(window), &win_x, &win_y);
    adjusted.x -= win_x;
    adjusted.y -= win_y;

    return vterm_write_mouse_event(vterm, &adjusted);
}

int
vwmterm_ON_KEYSTROKE(vk_object_t *object, int32_t keystroke)
{
    vwm_t           *vwm;
    vk_window_t     *window;
    vwmterm_data_t  *vwmterm_data;
    vterm_t         *vterm;
    int             width, height;
    int             history_sz;
    int             offset;

    window = VK_WINDOW(object);
    vwm = vwm_get_instance();

    if(keystroke == KEY_MOUSE)
    {
        MEVENT *me = vk_kmio_get_mouse_event();
        if(me == NULL) return 1;

        vwmterm_data = (vwmterm_data_t *)vk_widget_get_userptr(VK_WIDGET(window));
        vterm = vwmterm_data->vterm;

        if(me->bstate & BUTTON1_PRESSED)
        {
            if(vwmterm_data->frozen == 1)
            {
                vwmterm_exit_selection(vwmterm_data);
                return KMIO_HANDLED;
            }

            int win_y, win_x;
            int row, col;

            vk_widget_get_position(VK_WIDGET(window), &win_x, &win_y);
            row = me->y - win_y - 1;
            col = me->x - win_x - 1;

            vterm_wnd_size(vterm, &width, &height);

            if(row >= 0 && row < height && col >= 0 && col < width)
            {
                vwmterm_data->frozen = 3;
                vwmterm_data->sel_anchor_row = row;
                vwmterm_data->sel_anchor_col = col;
                vwmterm_data->sel_end_row = row;
                vwmterm_data->sel_end_col = col;
                return KMIO_HANDLED;
            }
        }

        if((me->bstate & REPORT_MOUSE_POSITION) &&
           (vwmterm_data->frozen == 3 || vwmterm_data->frozen == 2))
        {
            int win_y, win_x;
            int row, col;

            if(vwmterm_data->frozen == 3)
            {
                vwmterm_data->frozen = 2;
                vk_window_set_title(vwmterm_data->window,
                    " SELECT (Enter=Copy, Esc=Cancel) ");
            }

            vk_widget_get_position(VK_WIDGET(window), &win_x, &win_y);
            row = me->y - win_y - 1;
            col = me->x - win_x - 1;

            vterm_wnd_size(vterm, &width, &height);
            if(row < 0) row = 0;
            if(row >= height) row = height - 1;
            if(col < 0) col = 0;
            if(col >= width) col = width - 1;

            vwmterm_data->sel_end_row = row;
            vwmterm_data->sel_end_col = col;

            vwmterm_render_selection(vwmterm_data);
            return KMIO_HANDLED;
        }

        if((me->bstate & (BUTTON1_RELEASED | BUTTON1_CLICKED)) &&
           (vwmterm_data->frozen == 3 || vwmterm_data->frozen == 2))
        {
            if(vwmterm_data->frozen == 3)
            {
                vwmterm_data->frozen = 0;

                MEVENT click = *me;
                click.bstate = BUTTON1_CLICKED;
                vwmterm_write_mouse(vterm, window, &click);
            }
            else if(vwmterm_data->sel_anchor_row == vwmterm_data->sel_end_row &&
                    vwmterm_data->sel_anchor_col == vwmterm_data->sel_end_col)
            {
                vwmterm_exit_selection(vwmterm_data);
            }
            else
            {
                vwmterm_data->frozen = 1;
            }

            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON2_PRESSED)
        {
            if(clipboard != NULL && clipboard_len > 0)
            {
                for(size_t i = 0; i < clipboard_len; i++)
                    vterm_write_pipe(vterm, (uint32_t)clipboard[i]);
            }
            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON4_PRESSED)
        {
            if(vwmterm_write_mouse(vterm, window, me) > 0)
                return KMIO_HANDLED;

            vterm_wnd_size(vterm, &width, &height);
            history_sz = vterm_get_history_size(vterm);

            vwmterm_data->scroll_offset += 3;
            if(vwmterm_data->scroll_offset > history_sz - height)
                vwmterm_data->scroll_offset = history_sz - height;
            if(vwmterm_data->scroll_offset < 0)
                vwmterm_data->scroll_offset = 0;

            offset = history_sz - height - vwmterm_data->scroll_offset;
            if(offset < 0) offset = 0;

            vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
                VTERM_WND_RENDER_ALL);
            vk_window_update(vwmterm_data->window);
            vk_screen_refresh(vwm->screen);

            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON5_PRESSED)
        {
            if(vwmterm_write_mouse(vterm, window, me) > 0)
                return KMIO_HANDLED;

            if(vwmterm_data->scroll_offset == 0) return KMIO_HANDLED;

            vterm_wnd_size(vterm, &width, &height);
            history_sz = vterm_get_history_size(vterm);

            vwmterm_data->scroll_offset -= 3;
            if(vwmterm_data->scroll_offset <= 0)
            {
                vwmterm_data->scroll_offset = 0;
                vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
                vk_window_update(vwmterm_data->window);
                vk_screen_refresh(vwm->screen);
                return KMIO_HANDLED;
            }

            offset = history_sz - height - vwmterm_data->scroll_offset;
            if(offset < 0) offset = 0;

            vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
                VTERM_WND_RENDER_ALL);
            vk_window_update(vwmterm_data->window);
            vk_screen_refresh(vwm->screen);

            return KMIO_HANDLED;
        }

        return KMIO_HANDLED;
    }

    vwmterm_data = (vwmterm_data_t *)vk_widget_get_userptr(VK_WIDGET(window));
    vterm = vwmterm_data->vterm;

    if(keystroke == key_sel_up || keystroke == key_sel_dn ||
       keystroke == key_sel_left || keystroke == key_sel_right ||
       keystroke == key_sel_home || keystroke == key_sel_end)
    {
        vterm_wnd_size(vterm, &width, &height);

        if(!vwmterm_data->frozen)
            vwmterm_enter_selection(vwmterm_data);

        if(keystroke == key_sel_up)
        {
            if(vwmterm_data->sel_end_row > 0)
                vwmterm_data->sel_end_row--;
        }
        else if(keystroke == key_sel_dn)
        {
            if(vwmterm_data->sel_end_row < height - 1)
                vwmterm_data->sel_end_row++;
        }
        else if(keystroke == key_sel_left)
        {
            if(vwmterm_data->sel_end_col > 0)
                vwmterm_data->sel_end_col--;
            else if(vwmterm_data->sel_end_row > 0)
            {
                vwmterm_data->sel_end_row--;
                vwmterm_data->sel_end_col = width - 1;
            }
        }
        else if(keystroke == key_sel_right)
        {
            if(vwmterm_data->sel_end_col < width - 1)
                vwmterm_data->sel_end_col++;
            else if(vwmterm_data->sel_end_row < height - 1)
            {
                vwmterm_data->sel_end_row++;
                vwmterm_data->sel_end_col = 0;
            }
        }
        else if(keystroke == key_sel_home)
        {
            vwmterm_data->sel_end_col = 0;
        }
        else if(keystroke == key_sel_end)
        {
            vwmterm_data->sel_end_col = width - 1;
        }

        vwmterm_render_selection(vwmterm_data);
        return KMIO_HANDLED;
    }

    if((keystroke == KEY_ENTER_CR || keystroke == KEY_ENTER_LF ||
        keystroke == KEY_ENTER) && vwmterm_data->frozen)
    {
        vwmterm_copy_selection(vwmterm_data);
        vwmterm_exit_selection(vwmterm_data);
        return KMIO_HANDLED;
    }

    if(keystroke == 27 && vwmterm_data->frozen)
    {
        vwmterm_exit_selection(vwmterm_data);
        return KMIO_HANDLED;
    }

    if(keystroke == KEY_PASTE)
    {
        if(clipboard != NULL && clipboard_len > 0)
        {
            for(size_t i = 0; i < clipboard_len; i++)
                vterm_write_pipe(vterm, (uint32_t)clipboard[i]);
        }
        return KMIO_HANDLED;
    }

    if(vwmterm_data->frozen) return KMIO_HANDLED;

    if(keystroke == key_alt_pgup)
    {
        vterm_wnd_size(vterm, &width, &height);
        history_sz = vterm_get_history_size(vterm);

        vwmterm_data->scroll_offset += height;
        if(vwmterm_data->scroll_offset > history_sz - height)
            vwmterm_data->scroll_offset = history_sz - height;
        if(vwmterm_data->scroll_offset < 0)
            vwmterm_data->scroll_offset = 0;

        offset = history_sz - height - vwmterm_data->scroll_offset;
        if(offset < 0) offset = 0;

        vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
            VTERM_WND_RENDER_ALL);
        vk_window_update(vwmterm_data->window);
        vk_screen_refresh(vwm->screen);

        return KMIO_HANDLED;
    }

    if(keystroke == key_alt_pgdn)
    {
        if(vwmterm_data->scroll_offset == 0) return KMIO_HANDLED;

        vterm_wnd_size(vterm, &width, &height);
        history_sz = vterm_get_history_size(vterm);

        vwmterm_data->scroll_offset -= height;
        if(vwmterm_data->scroll_offset <= 0)
        {
            vwmterm_data->scroll_offset = 0;
            vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
            vk_window_update(vwmterm_data->window);
            vk_screen_refresh(vwm->screen);
            return KMIO_HANDLED;
        }

        offset = history_sz - height - vwmterm_data->scroll_offset;
        if(offset < 0) offset = 0;

        vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
            VTERM_WND_RENDER_ALL);
        vk_window_update(vwmterm_data->window);
        vk_screen_refresh(vwm->screen);

        return KMIO_HANDLED;
    }

    if(vwmterm_data->scroll_offset > 0)
    {
        vwmterm_data->scroll_offset = 0;
        vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
        vk_window_update(vwmterm_data->window);
        vk_screen_refresh(vwm->screen);
    }

    vterm_write_pipe(vterm, keystroke);

    return KMIO_HANDLED;
}

int
vwmterm_ON_SCREEN_RESIZED(vk_object_t *object, int event, void *anything)
{
    vwm_t           *vwm;
    vk_window_t     *window;
    int             scr_width, scr_height;

    (void)event;

    window = VK_WINDOW(object);
    vwm = vwm_get_instance();

    if(anything != NULL)
    {
        if(strcmp((char *)anything, "fullscreen") == 0)
        {
            getmaxyx(vk_screen_get_window(vwm->screen),
                scr_height, scr_width);
            vk_widget_resize(VK_WIDGET(window), scr_width, scr_height);
        }
    }

    return 0;
}


int
vwmterm_ON_RESIZE(vk_object_t *object, int event, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
	vterm_t         *vterm;
    vk_widget_t     *content;
    unsigned int    width;
    unsigned int    height;

    (void)event;

    /*
        anything is the vwmterm_data registered at init.c.  Route through
        it (not the raw vterm pointer) so that if vterm has been destroyed
        but a stale event slipped past vwm_cancel_drag_for_widget, we see
        vwmterm_data->vterm == NULL and bail safely.
    */
    vwmterm_data = (vwmterm_data_t *)anything;
    if(vwmterm_data == NULL) return 0;

    vterm = vwmterm_data->vterm;
    if(vterm == NULL) return 0;

    content = vk_window_get_child(VK_WINDOW(object));
    getmaxyx(vk_widget_get_canvas(content), height, width);
    vterm_resize(vterm, width, height);
    vterm_wnd_update(vterm, -1, 0, 0);

	return 0;
}

/*
    fires when libviper rebuilds the WINDOW backing this widget -- happens
    after a teleport, where vk_screen_teleport tears down the old ncurses
    SCREEN, creates a new one, and calls vk_widget_recreate on every
    surface widget.  the per-widget canvas WINDOW pointer libvterm cached
    at init time is now dead; rebind it to the freshly-recreated canvas
    and force a full redraw.
*/
int
vwmterm_ON_RECREATE(vk_object_t *object, int event, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
    vterm_t         *vterm;
    vk_widget_t     *content;
    vwm_t           *vwm;

    (void)event;

    vwmterm_data = (vwmterm_data_t *)anything;
    if(vwmterm_data == NULL) return 0;

    vterm = vwmterm_data->vterm;
    if(vterm == NULL) return 0;

    content = vk_window_get_child(VK_WINDOW(object));
    if(content == NULL) return 0;

    vterm_wnd_set(vterm, vk_widget_get_canvas(content));

    vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);

    vwm = vwm_get_instance();
    if(vwm != NULL)
    {
        vk_window_update(vwmterm_data->window);
        vk_screen_refresh(vwm->screen);
    }

    return 0;
}

int
vwmterm_ON_CLOSE(vk_object_t *object, int event, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
    pid_t           child_pid;

    (void)object;
    (void)event;

    vwmterm_data = (vwmterm_data_t*)anything;

    vwmterm_data->state = VWMTERM_STATE_EXITING;

    if(vwmterm_data->vterm != NULL)
    {
        child_pid = vterm_get_pid(vwmterm_data->vterm);

        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);

        /*
            Invalidate any in-flight drag targeting this window before
            we free the vterm.  Otherwise the dangling pointer baked
            into the ON_RESIZE handler at registration would drive a
            UAF inside vterm_buffer_realloc on the next mouse event.
        */
        vwm_cancel_drag_for_widget(VK_WIDGET(vwmterm_data->window));

        vterm_destroy(vwmterm_data->vterm);
        vwmterm_data->vterm = NULL;
    }

	return 0;
}
