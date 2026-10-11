#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <vdk.h>
#include <vkmio.h>
#include <vtty.h>

#include "vwmterm.h"
#include "events.h"
#include "pt_thread.h"

#include "../../vwm.h"
#include "../../private.h"
#include "../../panel.h"
#include "../../poll_input_thd.h"
#include "../../winman.h"
#include "../../manage_windows.h"

/* the title event this module relies on arrived in libvtty 11.2 */
#ifndef VTTY_MASK_TITLE
#error "vwm needs libvtty 11.2 or newer (VTTY_EVENT_TITLE)"
#endif

#define KEY_ENTER_CR    13
#define KEY_ENTER_LF    10

#define KEY_PASTE       0x561B

/* history lines scrolled per mouse-wheel notch.  the Alt+PgUp/PgDn
   hotkeys still page (a full window height); the wheel is fine-grained. */
#define VWMTERM_WHEEL_SCROLL_LINES  1

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

static void     vwmterm_scroll_drag_apply(vk_widget_t *widget, int mx, int my);
static void     vwmterm_scroll_render(vwmterm_data_t *vwmterm_data);
static void     vwmterm_osc52_copy(const char *buf, size_t len, char selection);
static void     vwmterm_xclip_copy(const char *buf, size_t len, char selection);
static void     vwmterm_host_clipboard_push(const char *buf, size_t len,
                    char selection);
static void     vwmterm_clipboard_take(char *buf, size_t len, char selection);
static void     vwmterm_vterm_hook(vtty_t *vterm, int event, void *anything);

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

    /* let the poll loop capture scrollbar-thumb drags so a release is never
       missed -- the thumb can't stick to the cursor after a sloppy let-go */
    vwm_set_scroll_drag_cb(vwmterm_scroll_drag_apply);
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
vwmterm_osc52_copy(const char *buf, size_t len, char selection)
{
    char            *enc;
    size_t          enc_len;
    size_t          i, j;
    unsigned char   a, b, c;
    char            pc;

    pc = (selection == '\0') ? 'c' : selection;

    if(buf == NULL || len == 0)
    {
        fprintf(stdout, "\033]52;%c;\033\\", pc);
        fflush(stdout);
        return;
    }

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

    fprintf(stdout, "\033]52;%c;", pc);
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
vwmterm_xclip_copy(const char *buf, size_t len, char selection)
{
    FILE        *fp;
    const char  *cmd;

    if(selection == 'p' || selection == 's')
        cmd = "xclip -selection primary 2>/dev/null";
    else
        cmd = "xclip -selection clipboard 2>/dev/null";

    fp = popen(cmd, "w");
    if(fp == NULL) return;

    if(buf != NULL && len > 0)
        fwrite(buf, 1, len, fp);

    pclose(fp);
}

static void
vwmterm_host_clipboard_push(const char *buf, size_t len, char selection)
{
    vwm_t   *vwm = vwm_get_instance();
    int     mode = (vwm != NULL)
        ? vwm->clipboard_mode
        : VWM_CLIPBOARD_NEVER;

    if(mode == VWM_CLIPBOARD_OSC52 || mode == VWM_CLIPBOARD_BOTH)
        vwmterm_osc52_copy(buf, len, selection);
    if(mode == VWM_CLIPBOARD_XCLIP || mode == VWM_CLIPBOARD_BOTH)
        vwmterm_xclip_copy(buf, len, selection);
}

/*
    take ownership of buf (NULL / len 0 clears).  always updates the
    internal paste buffer; host sync follows Settings clipboard_mode.
*/
static void
vwmterm_clipboard_take(char *buf, size_t len, char selection)
{
    if(clipboard != NULL) free(clipboard);
    clipboard = buf;
    clipboard_len = (buf != NULL) ? len : 0;

    vwmterm_host_clipboard_push(clipboard, clipboard_len, selection);
}

void
vwmterm_bind_vterm(vtty_t *vterm)
{
    if(vterm == NULL) return;

    vtty_install_hook(vterm, vwmterm_vterm_hook);
    vtty_set_event_mask(vterm, VTTY_MASK_CLIPBOARD | VTTY_MASK_TITLE);
}

/*
    Copy `src` into `dst` as text fit for a window border: control
    characters and bytes that are not valid in the locale's encoding are
    left out, and copying stops before the text would be wider than
    `max_cols` screen columns or longer than `dst_sz - 1` bytes.  A
    character is never split.  Returns the columns used; `*cut` (when
    not NULL) is set when something had to be left off the end.
*/
static int
vwmterm_title_copy(const char *src, char *dst, size_t dst_sz, int max_cols,
    bool *cut)
{
    mbstate_t   state;
    wchar_t     wc;
    size_t      used = 0;
    size_t      left = strlen(src);
    size_t      n;
    int         cols = 0;
    int         w;

    memset(&state, 0, sizeof(state));
    if(cut != NULL) *cut = false;
    if(dst_sz == 0) return 0;
    dst[0] = '\0';

    while(left > 0)
    {
        n = mbrtowc(&wc, src, left, &state);

        /* not a character in this encoding: skip the byte and resync */
        if(n == (size_t)-1 || n == (size_t)-2 || n == 0)
        {
            memset(&state, 0, sizeof(state));
            src++;
            left--;
            continue;
        }

        w = wcwidth(wc);

        /* a control character (no width to speak of) is dropped */
        if(w < 0)
        {
            src += n;
            left -= n;
            continue;
        }

        /* no room for this one: stop here, on a character boundary */
        if(cols + w > max_cols || used + n > dst_sz - 1)
        {
            if(cut != NULL) *cut = true;
            break;
        }

        memcpy(dst + used, src, n);
        used += n;
        cols += w;
        src += n;
        left -= n;
    }

    dst[used] = '\0';

    return cols;
}

/* columns of the top border kept clear of the title: the corners, and
   the minimize / close buttons on the right.  The title is centered,
   so the same room is left on the left. */
#define VWMTERM_TITLE_KEEP_CLEAR    18

/* the least room worth showing a program's title in; with less, the
   app's name stands alone */
#define VWMTERM_TITLE_MIN_COLS      4

/*
    Work out the title this terminal's window should show and put it
    there: the app's name and, to its right, the title the program has
    set for itself --

        " Claude Code: fixing the tests "

    The program's part is cut to what the border has room for, with
    "..." to show it was cut, and is left off altogether when there is
    hardly any room.  Called when the program changes its title and
    when the window changes width.  In selection mode the border shows
    the selection prompt; the new title is kept for when that ends.
*/
static void
vwmterm_title_apply(vwmterm_data_t *vwmterm_data)
{
    char    name[sizeof(vwmterm_data->app_title)];
    char    part[sizeof(vwmterm_data->osc_title)];
    char    title[sizeof(vwmterm_data->title)];
    size_t  len;
    bool    cut = false;
    int     width, height;
    int     room;
    int     name_cols;

    if(vwmterm_data == NULL || vwmterm_data->window == NULL) return;

    /* a fullscreen terminal has no border and so no title */
    if(vwmterm_data->app_title[0] == '\0') return;

    /* the app's name without the space each side it is stored with */
    snprintf(name, sizeof(name), "%s", vwmterm_data->app_title + 1);
    len = strlen(name);
    if(len > 0 && name[len - 1] == ' ') name[len - 1] = '\0';

    vk_widget_get_metrics(VK_WIDGET(vwmterm_data->window), &width, &height);
    name_cols = (int)mbstowcs(NULL, name, 0);
    if(name_cols < 0) name_cols = (int)strlen(name);

    /* columns left for the program's title after " name: " and the
       closing space */
    room = width - VWMTERM_TITLE_KEEP_CLEAR - name_cols - 4;

    part[0] = '\0';
    if(vwmterm_data->osc_title[0] != '\0' && room >= VWMTERM_TITLE_MIN_COLS)
    {
        vwmterm_title_copy(vwmterm_data->osc_title, part, sizeof(part),
            room, &cut);

        /* it did not all fit: make room for the dots and say so */
        if(cut)
        {
            vwmterm_title_copy(vwmterm_data->osc_title, part,
                sizeof(part) - 3, room - 3, NULL);
            strcat(part, "...");
        }
    }

    if(part[0] != '\0')
        snprintf(title, sizeof(title), " %s: %s ", name, part);
    else
        snprintf(title, sizeof(title), "%s", vwmterm_data->app_title);

    /* nothing changed: leave the window alone */
    if(strcmp(title, vwmterm_data->title) == 0) return;

    snprintf(vwmterm_data->title, sizeof(vwmterm_data->title), "%s", title);

    /* frozen: selection mode owns the border until it ends, and puts
       vwmterm_data->title back itself */
    if(vwmterm_data->frozen == 0)
        vk_window_set_title(vwmterm_data->window, vwmterm_data->title);

    /* painted with the terminal's next update */
    vwmterm_data->redraw_pending = 1;

    /* Manage Desktop lists windows by title: keep an open one in step */
    vwm_manage_windows_sync();
}

/*
    The program in this terminal set its window title (OSC 0 or OSC 2),
    or gave it back with an empty one.  libvtty only reports a real
    change.  Keep a cleaned copy and re-work the window's title.
*/
static void
vwmterm_title_changed(vtty_t *vterm, const char *osc_title)
{
    vwmterm_data_t  *vwmterm_data;

    vwmterm_data = (vwmterm_data_t *)vtty_get_userptr(vterm);
    if(vwmterm_data == NULL) return;

    /* any width will do here: this copy is only cleaned, not fitted */
    vwmterm_title_copy((osc_title != NULL) ? osc_title : "",
        vwmterm_data->osc_title, sizeof(vwmterm_data->osc_title),
        (int)sizeof(vwmterm_data->osc_title), NULL);

    vwmterm_title_apply(vwmterm_data);
}

static void
vwmterm_vterm_hook(vtty_t *vterm, int event, void *anything)
{
    vtty_clipboard_t    *clip;
    char                *copy;
    char                sel;

    /* the program named its window: `anything` is the new title */
    if(event == VTTY_EVENT_TITLE)
    {
        vwmterm_title_changed(vterm, (const char *)anything);
        return;
    }

    if(event != VTTY_EVENT_CLIPBOARD) return;
    if(anything == NULL) return;

    clip = (vtty_clipboard_t *)anything;
    sel = (clip->selection == '\0') ? 'c' : clip->selection;

    if(clip->data == NULL || clip->len == 0)
    {
        vwmterm_clipboard_take(NULL, 0, sel);
        return;
    }

    copy = (char *)malloc(clip->len + 1);
    if(copy == NULL) return;

    memcpy(copy, clip->data, clip->len);
    copy[clip->len] = '\0';
    vwmterm_clipboard_take(copy, clip->len, sel);
}

/*
    Toggle A_REVERSE on [cs, ce] of a rendered row without rewriting
    glyphs.  The old getcchar/setcchar/mvwadd_wch path destroyed
    double-width cells (emoji, CJK): the trail column is a blank
    continuation that libvtty's painter skips, and rewriting it
    orphaned the wide cchar and scrambled the rest of the line.  Box
    drawing and other non-ASCII also survived attribute-only changes
    more reliably than a full cchar round-trip.

    Wide glyphs: one mvwchgat covering both columns (same pattern as
    libvtty's cursor paint), then skip the trail so we never touch it
    as a standalone cell.
*/
static void
vwmterm_highlight_span(WINDOW *win, int row, int cs, int ce)
{
    int     c;

    if(win == NULL || cs > ce) return;

    for(c = cs; c <= ce; )
    {
        cchar_t     cc;
        wchar_t     wch[CCHARW_MAX];
        attr_t      attrs;
        short       pair;
        int         n = 1;

        if(mvwin_wch(win, row, c, &cc) == ERR)
        {
            c++;
            continue;
        }

        getcchar(&cc, wch, &attrs, &pair, NULL);

        if(wch[0] > 0x7F && wcwidth(wch[0]) == 2)
            n = 2;

        mvwchgat(win, row, c, n, attrs ^ A_REVERSE, pair, NULL);
        c += n;
    }
}

/*
    The vterm's usable grid size.  vtty_wnd_size reports the attached canvas,
    which on a bordered terminal is one column wider than the vterm grid -- that
    extra column holds the scrollbar.  Selection bounds and scrollbar
    hit-testing work in grid coordinates, so they use this rather than
    vtty_wnd_size directly (the scrollbar then sits at exactly column *width).
*/
static void
vwmterm_grid_size(vwmterm_data_t *vwmterm_data, int *width, int *height)
{
    int     w = 0, h = 0;

    vtty_wnd_size(vwmterm_data->vterm, &w, &h);
    if(vwmterm_data->scroller != NULL && w > 0)
        w -= 1;                             /* reserved scrollbar column */

    if(width != NULL) *width = w;
    if(height != NULL) *height = h;
}

/*
    The selection as (first row, first column) .. (last row, last
    column) in reading order.  It is kept as the cell where it was
    started and the cell it has been dragged to, and the second can be
    before the first.
*/
static void
vwmterm_selection_span(vwmterm_data_t *vwmterm_data,
    int *r1, int *c1, int *r2, int *c2)
{
    if(vwmterm_data->sel_anchor_row < vwmterm_data->sel_end_row ||
       (vwmterm_data->sel_anchor_row == vwmterm_data->sel_end_row &&
        vwmterm_data->sel_anchor_col <= vwmterm_data->sel_end_col))
    {
        *r1 = vwmterm_data->sel_anchor_row;
        *c1 = vwmterm_data->sel_anchor_col;
        *r2 = vwmterm_data->sel_end_row;
        *c2 = vwmterm_data->sel_end_col;
    }
    else
    {
        *r1 = vwmterm_data->sel_end_row;
        *c1 = vwmterm_data->sel_end_col;
        *r2 = vwmterm_data->sel_anchor_row;
        *c2 = vwmterm_data->sel_anchor_col;
    }
}

/*
    Scroll the view `lines` rows back into history (negative: forward,
    toward the live screen) and repaint.  The position is held between
    the live screen and the oldest row kept.  The wheel and Alt+PgUp /
    Alt+PgDn come here; they differ only in how far they go.
*/
static void
vwmterm_scroll_by(vwmterm_data_t *vwmterm_data, int lines)
{
    int used = vtty_get_history_used(vwmterm_data->vterm);

    vwmterm_data->scroll_offset += lines;
    if(vwmterm_data->scroll_offset > used)
        vwmterm_data->scroll_offset = used;
    if(vwmterm_data->scroll_offset < 0)
        vwmterm_data->scroll_offset = 0;

    /* at offset 0 this paints the live screen */
    vwmterm_scroll_render(vwmterm_data);
    vwmterm_window_update(vwmterm_data);
    vk_screen_refresh(vwm_get_instance()->screen);
}

static void
vwmterm_render_selection(vwmterm_data_t *vwmterm_data)
{
    vwm_t       *vwm;
    vtty_t      *vterm;
    WINDOW      *win;
    int         width, height;
    int         r1, c1, r2, c2;

    vwm = vwm_get_instance();
    vterm = vwmterm_data->vterm;
    win = vtty_wnd_get(vterm);
    vwmterm_grid_size(vwmterm_data, &width, &height);

    /* render at the current scroll position, not always the live screen --
       otherwise starting/dragging a selection while scrolled back snaps the
       view to the bottom and the highlighted rows are the wrong ones */
    vwmterm_scroll_render(vwmterm_data);

    vwmterm_selection_span(vwmterm_data, &r1, &c1, &r2, &c2);

    for(int r = r1; r <= r2; r++)
    {
        int cs = (r == r1) ? c1 : 0;
        int ce = (r == r2) ? c2 : width - 1;

        vwmterm_highlight_span(win, r, cs, ce);
    }

    vwmterm_window_update(vwmterm_data);
    vk_screen_refresh(vwm->screen);
}

static void
vwmterm_copy_selection(vwmterm_data_t *vwmterm_data)
{
    vtty_t          *vterm;
    vtty_cell_t     **cells;
    int             rows, cols;
    int             r1, c1, r2, c2;
    char            *buf;
    size_t          buf_sz;
    size_t          pos;
    char            mb[MB_LEN_MAX];
    int             len;

    vterm = vwmterm_data->vterm;
    /* copy from the same view the user sees: the composed scrollback matrix
       when scrolled back, else the live buffer */
    if(vwmterm_data->scroll_offset > 0)
        cells = vtty_copy_scrollback(vterm, vwmterm_data->scroll_offset,
                    &rows, &cols);
    else
        cells = vtty_copy_buffer(vterm, &rows, &cols);
    if(cells == NULL) return;

    vwmterm_selection_span(vwmterm_data, &r1, &c1, &r2, &c2);

    if(r1 < 0) r1 = 0;
    if(r2 >= rows) r2 = rows - 1;
    if(c1 < 0) c1 = 0;
    if(c2 >= cols) c2 = cols - 1;

    /* nothing selected once clamped to the grid (an empty grid, or a
       selection wholly outside it).  Also keeps the size below from
       being computed from a negative row count. */
    if(r2 < r1 || cols < 1)
    {
        for(int r = 0; r < rows; r++) free(cells[r]);
        free(cells);
        return;
    }

    /* room for the worst case: every cell a full-length multibyte
       character, plus a newline per row.  That is hundreds of KB for a
       large selection, so the allocation can fail: give up the copy
       rather than write through NULL. */
    buf_sz = (size_t)(r2 - r1 + 1) * ((size_t)cols * MB_LEN_MAX + 1);
    buf = (char *)calloc(1, buf_sz);
    if(buf == NULL)
    {
        for(int r = 0; r < rows; r++) free(cells[r]);
        free(cells);
        return;
    }
    pos = 0;

    for(int r = r1; r <= r2; r++)
    {
        int col_start = (r == r1) ? c1 : 0;
        int col_end = (r == r2) ? c2 : cols - 1;

        for(int c = col_start; c <= col_end; c++)
        {
            wchar_t wch = cells[r][c].wch[0];

            /*
                libvtty stores a blank continuation in the cell after a
                double-width glyph.  Skip it so copy doesn't inject a
                spurious space (and so a selection that only covers the
                trail half doesn't invent a character).
            */
            if(c > 0
                && cells[r][c - 1].wch[0] > 0x7F
                && wcwidth(cells[r][c - 1].wch[0]) == 2)
            {
                continue;
            }

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

    vwmterm_clipboard_take(buf, pos, 'c');

    for(int r = 0; r < rows; r++) free(cells[r]);
    free(cells);
}

static void
vwmterm_enter_selection(vwmterm_data_t *vwmterm_data)
{
    vtty_t *vterm = vwmterm_data->vterm;
    int     col, row;

    vtty_get_cursor_position(vterm, &col, &row);

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

    vwm = vwm_get_instance();

    vwmterm_data->frozen = 0;
    vwmterm_wake(vwmterm_data);     /* reading again: catch up now */

    /* leaving selection: stay at the scroll position the user was viewing
       (scroll-aware) rather than jumping to the live screen */
    vwmterm_scroll_render(vwmterm_data);

    vk_window_set_title(vwmterm_data->window, vwmterm_data->title);

    vwm_panel_set_status(VWM_WINDOW_HELP);

    vwmterm_window_update(vwmterm_data);
    vk_screen_refresh(vwm->screen);
}

static int
vwmterm_write_mouse(vtty_t *vterm, vk_window_t *window, MEVENT *me)
{
    int win_x, win_y;
    MEVENT adjusted = *me;

    vk_widget_get_position(VK_WIDGET(window), &win_x, &win_y);
    adjusted.x -= win_x;
    adjusted.y -= win_y;

    return vtty_write_mouse_event(vterm, &adjusted);
}

/*
    Update the vterm window.  The scrollbar is redrawn by vwmterm_decorate
    (init.c), which libviper invokes on every window paint, so a plain window
    update refreshes the bar in lockstep with the frame -- including on a bare
    focus change (which repaints the frame but not the content-attached
    scroller).
*/
void
vwmterm_window_update(vwmterm_data_t *vwmterm_data)
{
    vk_window_update(vwmterm_data->window);
}

/*
    Render the vterm at the current scroll_offset.  scroll_offset is the
    scroll-back distance in rows; libvtty's vtty_wnd_scrollback composes the
    newest N evicted rows above the head of the live screen -- so even less
    than one screenful of history shows, with the live buffer beneath it.
    scroll_offset 0 is the live buffer (drawn with the cursor).
*/
static void
vwmterm_scroll_render(vwmterm_data_t *vwmterm_data)
{
    vtty_t      *vterm = vwmterm_data->vterm;

    if(vwmterm_data->scroll_offset > 0)
    {
        vtty_wnd_scrollback(vterm, vwmterm_data->scroll_offset,
            VTTY_WND_RENDER_ALL);
    }
    else
    {
        vtty_wnd_update(vterm, -1, 0, VTTY_WND_RENDER_ALL);
    }
}

/*
    Move the scroll position from a click or drag on the scrollbar column.
    row is the 0-based interior row under the pointer.  On a press the top and
    bottom rows are the up / down arrows and step one line; any other row --
    and every drag update -- picks a track position, top = oldest history,
    bottom = live.  This is the inverse of vk_scroller's thumb placement
    (its viewport is height - 2, with an arrow at each end).
*/
static void
vwmterm_scrollbar_to(vwmterm_data_t *vwmterm_data, int row, int is_press)
{
    vtty_t      *vterm = vwmterm_data->vterm;
    vwm_t       *vwm;
    int         width, height;
    int         used;
    int         track_len, p, scroll_range, scroll_y;
    int         new_offset;

    vwmterm_grid_size(vwmterm_data, &width, &height);
    used = vtty_get_history_used(vterm);

    if(vtty_get_active_buffer(vterm) == VTTY_BUF_ALTERNATE) return;
    if(used <= 0) return;                       /* nothing scrolled off yet */

    if(is_press && row <= 0)
    {
        new_offset = vwmterm_data->scroll_offset + 1;           /* up arrow */
    }
    else if(is_press && row >= height - 1)
    {
        new_offset = vwmterm_data->scroll_offset - 1;           /* down arrow */
    }
    else
    {
        track_len = height - 2;                 /* vk_scroller's viewport */
        if(row < 1) row = 1;
        if(row > height - 2) row = height - 2;
        p = row - 1;                            /* 0 .. track_len-1 */
        scroll_range = used;
        scroll_y = (track_len > 1) ? p * scroll_range / (track_len - 1) : 0;
        new_offset = used - scroll_y;
    }

    if(new_offset > used) new_offset = used;
    if(new_offset < 0) new_offset = 0;

    if(new_offset == vwmterm_data->scroll_offset) return;

    vwmterm_data->scroll_offset = new_offset;

    vwmterm_scroll_render(vwmterm_data);
    vwmterm_window_update(vwmterm_data);

    vwm = vwm_get_instance();
    if(vwm != NULL) vk_screen_refresh(vwm->screen);
}

/*
    Callback for a captured scrollbar-thumb drag (registered via
    vwm_set_scroll_drag_cb).  The poll loop owns the mouse from the initial
    press until the button is released -- even if the pointer wanders off the
    window -- and feeds us the live cursor position here, which we map to a
    track row and scroll.  So the thumb follows the pointer smoothly and lets
    go the instant the button does.
*/
static void
vwmterm_scroll_drag_apply(vk_widget_t *widget, int mx, int my)
{
    vwmterm_data_t  *vwmterm_data;
    int             win_x, win_y;

    (void)mx;

    vwmterm_data = (vwmterm_data_t *)vk_widget_get_userptr(widget);
    if(vwmterm_data == NULL) return;

    vk_widget_get_position(widget, &win_x, &win_y);

    vwmterm_scrollbar_to(vwmterm_data, my - win_y - 1, 0);
}

int
vwmterm_ON_KEYSTROKE(vk_object_t *object, int32_t keystroke)
{
    vwm_t           *vwm;
    vk_window_t     *window;
    vwmterm_data_t  *vwmterm_data;
    vtty_t          *vterm;
    int             width, height;

    window = VK_WINDOW(object);
    vwm = vwm_get_instance();

    if(keystroke == KEY_MOUSE)
    {
        MEVENT *me = vk_kmio_get_mouse_event();
        if(me == NULL) return 1;

        vwmterm_data = (vwmterm_data_t *)vk_widget_get_userptr(VK_WIDGET(window));
        vterm = vwmterm_data->vterm;

        /*
            Scrollbar interaction.  The bar occupies the interior column just
            right of the vterm grid (col == vterm width); it is vwm chrome, so
            handle press / drag / release here -- ahead of selection and of any
            pass-through to the child.  A press on the track starts a drag and
            jumps to it; the arrows step one line.  Skipped in selection mode
            and for fullscreen terminals (which have no scrollbar).
        */
        if(vwmterm_data->scroller != NULL && !vwmterm_data->frozen &&
           (me->bstate & BUTTON1_PRESSED))
        {
            int sb_win_x, sb_win_y;
            int sb_row, sb_col;
            int sb_w, sb_h;

            vk_widget_get_position(VK_WIDGET(window), &sb_win_x, &sb_win_y);
            sb_row = me->y - sb_win_y - 1;
            sb_col = me->x - sb_win_x - 1;
            vwmterm_grid_size(vwmterm_data, &sb_w, &sb_h);

            if(sb_col == sb_w && sb_row >= 0 && sb_row < sb_h)
            {
                /* arrows line-step; a press on the track jumps there and hands
                   the drag to the poll loop (vwm_begin_scroll_drag), which
                   captures the mouse and feeds motion to
                   vwmterm_scroll_drag_apply until release -- so the thumb can't
                   stay glued to the pointer after a sloppy let-go */
                if(sb_row > 0 && sb_row < sb_h - 1)
                    vwm_begin_scroll_drag(VK_WIDGET(window));
                vwmterm_scrollbar_to(vwmterm_data, sb_row, 1);
                return KMIO_HANDLED;
            }
        }

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

            vwmterm_grid_size(vwmterm_data, &width, &height);

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

            vwmterm_grid_size(vwmterm_data, &width, &height);
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
                vwmterm_wake(vwmterm_data); /* reading again */

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
                vtty_write_data(vterm, clipboard, clipboard_len);
            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON4_PRESSED)
        {
            if(vwmterm_write_mouse(vterm, window, me) > 0)
                return KMIO_HANDLED;

            if(vtty_get_active_buffer(vterm) == VTTY_BUF_ALTERNATE)
                return KMIO_HANDLED;

            vwmterm_scroll_by(vwmterm_data, VWMTERM_WHEEL_SCROLL_LINES);

            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON5_PRESSED)
        {
            if(vwmterm_write_mouse(vterm, window, me) > 0)
                return KMIO_HANDLED;

            if(vtty_get_active_buffer(vterm) == VTTY_BUF_ALTERNATE)
                return KMIO_HANDLED;

            /* already on the live screen: nothing to scroll toward */
            if(vwmterm_data->scroll_offset == 0) return KMIO_HANDLED;

            vwmterm_scroll_by(vwmterm_data, -VWMTERM_WHEEL_SCROLL_LINES);

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
        vwmterm_grid_size(vwmterm_data, &width, &height);

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
            vtty_write_data(vterm, clipboard, clipboard_len);
        return KMIO_HANDLED;
    }

    if(keystroke == VK_KMIO_PASTE)
    {
        size_t      plen = 0;
        const char  *pbuf = vk_kmio_get_paste(&plen);

        if(pbuf != NULL && plen > 0)
            vtty_write_data(vterm, pbuf, plen);
        return KMIO_HANDLED;
    }

    if(vwmterm_data->frozen) return KMIO_HANDLED;

    if(keystroke == key_alt_pgup)
    {
        if(vtty_get_active_buffer(vterm) == VTTY_BUF_ALTERNATE)
            return KMIO_HANDLED;

        /* a screenful at a time */
        vwmterm_grid_size(vwmterm_data, &width, &height);
        vwmterm_scroll_by(vwmterm_data, height);

        return KMIO_HANDLED;
    }

    if(keystroke == key_alt_pgdn)
    {
        if(vtty_get_active_buffer(vterm) == VTTY_BUF_ALTERNATE)
            return KMIO_HANDLED;

        if(vwmterm_data->scroll_offset == 0) return KMIO_HANDLED;

        vwmterm_grid_size(vwmterm_data, &width, &height);
        vwmterm_scroll_by(vwmterm_data, -height);

        return KMIO_HANDLED;
    }

    if(vwmterm_data->scroll_offset > 0)
    {
        vwmterm_data->scroll_offset = 0;
        vtty_wnd_update(vterm, -1, 0, VTTY_WND_RENDER_ALL);
        vwmterm_window_update(vwmterm_data);
        vk_screen_refresh(vwm->screen);
    }

    vtty_write_pipe(vterm, keystroke);

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
	vtty_t          *vterm;
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

    /* the scrollbar (if any) owns the content's last column; the vterm takes
       the remaining width-1, and the bar is re-fitted to the new height */
    if(vwmterm_data->scroller != NULL)
    {
        vtty_resize(vterm, width - 1, height);
        vk_widget_resize(VK_WIDGET(vwmterm_data->scroller), 1, height);
        vk_widget_move(VK_WIDGET(vwmterm_data->scroller), width - 1, 0);
    }
    else
    {
        vtty_resize(vterm, width, height);
    }

    vtty_wnd_update(vterm, -1, 0, 0);

    /* a wider or narrower border has room for more or less of the
       program's title */
    vwmterm_title_apply(vwmterm_data);

	return 0;
}

/*
    fires when libviper rebuilds the WINDOW backing this widget -- happens
    after a teleport, where vk_screen_teleport tears down the old ncurses
    SCREEN, creates a new one, and calls vk_widget_recreate on every
    surface widget.  the per-widget canvas WINDOW pointer libvtty cached
    at init time is now dead; rebind it to the freshly-recreated canvas
    and force a full redraw.
*/
int
vwmterm_ON_RECREATE(vk_object_t *object, int event, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
    vtty_t          *vterm;
    vk_widget_t     *content;
    vwm_t           *vwm;

    (void)event;

    vwmterm_data = (vwmterm_data_t *)anything;
    if(vwmterm_data == NULL) return 0;

    vterm = vwmterm_data->vterm;
    if(vterm == NULL) return 0;

    content = vk_window_get_child(VK_WINDOW(object));
    if(content == NULL) return 0;

    vtty_wnd_set(vterm, vk_widget_get_canvas(content));

    /* the teleport rebuilt the content's canvas; re-point the scroller at it
       and rebuild the scroller's own backing window (a plain widget's
       recreate does not follow its attached scroller) */
    if(vwmterm_data->scroller != NULL)
    {
        vk_widget_set_surface(VK_WIDGET(vwmterm_data->scroller),
            vk_widget_get_canvas(content));
        vk_widget_recreate(VK_WIDGET(vwmterm_data->scroller));
    }

    vtty_wnd_update(vterm, -1, 0, VTTY_WND_RENDER_ALL);

    vwm = vwm_get_instance();
    if(vwm != NULL)
    {
        vwmterm_window_update(vwmterm_data);
        vk_screen_refresh(vwm->screen);
    }

    return 0;
}

/* how long a program gets to exit after a hang-up before it is killed */
#define VWMTERM_HANGUP_GRACE_MS     300

/*
    VWM_EVENT_ON_HANGUP: the session is ending.  Tell the program in
    this terminal the way a closing terminal would -- SIGHUP -- so a
    shell can save its history and pass the word to its own jobs.  It
    is not waited for here; ON_CLOSE does that.
*/
int
vwmterm_ON_HANGUP(vk_object_t *object, int event, void *anything)
{
    vwmterm_data_t  *vwmterm_data = (vwmterm_data_t *)anything;

    (void)object;
    (void)event;

    if(vwmterm_data == NULL || vwmterm_data->vterm == NULL) return 0;

    kill(vtty_get_pid(vwmterm_data->vterm), SIGHUP);

    return 0;
}

/*
    End the program in a terminal: hang up, give it a moment, and only
    then kill it.  It used to be killed outright, which cost a shell its
    history and left its jobs to be reaped by whoever noticed.  A
    program that already exited (the usual case after ON_HANGUP, or
    when the user typed "exit") is collected at once.
*/
static void
vwmterm_end_child(pid_t child_pid)
{
    int     waited_ms;

    if(child_pid <= 0) return;

    kill(child_pid, SIGHUP);

    for(waited_ms = 0; waited_ms < VWMTERM_HANGUP_GRACE_MS; waited_ms += 10)
    {
        /* gone (or never ours to wait for): nothing more to do */
        if(waitpid(child_pid, NULL, WNOHANG) != 0) return;

        usleep(10000);
    }

    /* it ignored the hang-up */
    kill(child_pid, SIGKILL);
    waitpid(child_pid, NULL, 0);
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

    /*
        The scroll bar stays attached and is destroyed with the window's
        content (libviper 10: an attached scroller goes with its host).
        Forget it here so that nothing drives it between now and then.
    */
    vwmterm_data->scroller = NULL;

    if(vwmterm_data->vterm != NULL)
    {
        child_pid = vtty_get_pid(vwmterm_data->vterm);

        vwmterm_end_child(child_pid);

        /*
            Invalidate any in-flight drag targeting this window before
            we free the vterm.  Otherwise the dangling pointer baked
            into the ON_RESIZE handler at registration would drive a
            UAF inside vtty_buffer_realloc on the next mouse event.
        */
        vwm_cancel_drag_for_widget(VK_WIDGET(vwmterm_data->window));

        vtty_destroy(vwmterm_data->vterm);
        vwmterm_data->vterm = NULL;
    }

	return 0;
}
