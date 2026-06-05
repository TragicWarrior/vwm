#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <viper.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"

#define KEY_ENTER_CR    13
#define KEY_ENTER_LF    10

/*
    Alt+Shift+V paste is an ESC + 'V' two-byte sequence handled by viper's
    escape reader, not by ncurses keypad, so the encoding is fixed.
*/
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
    vterm_t     *vterm;
    WINDOW      *win;
    int         width, height;
    int         r1, c1, r2, c2;

    vterm = vwmterm_data->vterm;
    win = vterm_wnd_get(vterm);
    vterm_wnd_size(vterm, &width, &height);

    vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);

    // normalize selection so (r1,c1) <= (r2,c2)
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

    viper_window_redraw(vwmterm_data->vwnd);
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

    // normalize
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

    // clamp to buffer bounds
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

        // trim trailing spaces on each line
        while(pos > 0 && buf[pos - 1] == ' ') pos--;

        if(r < r2)
            buf[pos++] = '\n';
    }

    buf[pos] = '\0';

    // store in clipboard
    if(clipboard != NULL) free(clipboard);
    clipboard = buf;
    clipboard_len = pos;

    // free the cell buffer copy
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

    viper_window_set_title(vwmterm_data->vwnd,
        " SELECT (Enter=Copy, Esc=Cancel) ");
}

static void
vwmterm_exit_selection(vwmterm_data_t *vwmterm_data)
{
    vterm_t *vterm = vwmterm_data->vterm;

    vwmterm_data->frozen = 0;

    vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
    viper_window_set_title(vwmterm_data->vwnd,
        " VTerm (Alt+PgUp/PgDn, Alt+Shft+V=Paste) ");
    viper_window_redraw(vwmterm_data->vwnd);
}

int
vwmterm_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd)
{
    vwmterm_data_t  *vwmterm_data;
    vterm_t         *vterm;
    int             width, height;
    int             history_sz;
    int             offset;

    if(keystroke == KEY_MOUSE)
    {
        MEVENT *me = viper_kmio_get_mouse_event();
        if(me == NULL) return 1;

        vwmterm_data = (vwmterm_data_t *)viper_window_get_userptr(vwnd);
        vterm = vwmterm_data->vterm;

        // Button1 press: cancel active selection or record potential drag start
        if(me->bstate & BUTTON1_PRESSED)
        {
            if(vwmterm_data->frozen == 1)
            {
                vwmterm_exit_selection(vwmterm_data);
                return KMIO_HANDLED;
            }

            int frame_y, frame_x;
            int row, col;

            getbegyx(WINDOW_FRAME(vwnd), frame_y, frame_x);
            row = me->y - frame_y - 1;
            col = me->x - frame_x - 1;

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

        // drag: start or continue mouse selection
        if((me->bstate & REPORT_MOUSE_POSITION) &&
           (vwmterm_data->frozen == 3 || vwmterm_data->frozen == 2))
        {
            int frame_y, frame_x;
            int row, col;

            if(vwmterm_data->frozen == 3)
            {
                vwmterm_data->frozen = 2;
                viper_window_set_title(vwmterm_data->vwnd,
                    " SELECT (Enter=Copy, Esc=Cancel) ");
            }

            getbegyx(WINDOW_FRAME(vwnd), frame_y, frame_x);
            row = me->y - frame_y - 1;
            col = me->x - frame_x - 1;

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

        // button release or click: finalize drag selection or cancel single click
        if((me->bstate & (BUTTON1_RELEASED | BUTTON1_CLICKED)) &&
           (vwmterm_data->frozen == 3 || vwmterm_data->frozen == 2))
        {
            if(vwmterm_data->frozen == 3)
            {
                vwmterm_data->frozen = 0;
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

        // middle click: paste
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
            viper_window_redraw(vwmterm_data->vwnd);

            return KMIO_HANDLED;
        }

        if(me->bstate & BUTTON5_PRESSED)
        {
            if(vwmterm_data->scroll_offset == 0) return KMIO_HANDLED;

            vterm_wnd_size(vterm, &width, &height);
            history_sz = vterm_get_history_size(vterm);

            vwmterm_data->scroll_offset -= 3;
            if(vwmterm_data->scroll_offset <= 0)
            {
                vwmterm_data->scroll_offset = 0;
                vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
                viper_window_redraw(vwmterm_data->vwnd);
                return KMIO_HANDLED;
            }

            offset = history_sz - height - vwmterm_data->scroll_offset;
            if(offset < 0) offset = 0;

            vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
                VTERM_WND_RENDER_ALL);
            viper_window_redraw(vwmterm_data->vwnd);

            return KMIO_HANDLED;
        }

        return KMIO_HANDLED;
    }

    vwmterm_data = (vwmterm_data_t *)viper_window_get_userptr(vwnd);
    vterm = vwmterm_data->vterm;


    // --- selection mode keys ---

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

    // copy selection
    if((keystroke == KEY_ENTER_CR || keystroke == KEY_ENTER_LF ||
        keystroke == KEY_ENTER) && vwmterm_data->frozen)
    {
        vwmterm_copy_selection(vwmterm_data);
        vwmterm_exit_selection(vwmterm_data);
        return KMIO_HANDLED;
    }

    // cancel selection
    if(keystroke == 27 && vwmterm_data->frozen)
    {
        vwmterm_exit_selection(vwmterm_data);
        return KMIO_HANDLED;
    }

    // paste clipboard
    if(keystroke == KEY_PASTE)
    {
        if(clipboard != NULL && clipboard_len > 0)
        {
            for(size_t i = 0; i < clipboard_len; i++)
                vterm_write_pipe(vterm, (uint32_t)clipboard[i]);
        }
        return KMIO_HANDLED;
    }

    // ignore other keys while frozen
    if(vwmterm_data->frozen) return KMIO_HANDLED;

    // --- scrollback keys ---

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
        viper_window_redraw(vwmterm_data->vwnd);

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
            viper_window_redraw(vwmterm_data->vwnd);
            return KMIO_HANDLED;
        }

        offset = history_sz - height - vwmterm_data->scroll_offset;
        if(offset < 0) offset = 0;

        vterm_wnd_update(vterm, VTERM_BUF_HISTORY, offset,
            VTERM_WND_RENDER_ALL);
        viper_window_redraw(vwmterm_data->vwnd);

        return KMIO_HANDLED;
    }

    if(vwmterm_data->scroll_offset > 0)
    {
        vwmterm_data->scroll_offset = 0;
        vterm_wnd_update(vterm, -1, 0, VTERM_WND_RENDER_ALL);
        viper_window_redraw(vwmterm_data->vwnd);
    }

    vterm_write_pipe(vterm, keystroke);

    return KMIO_HANDLED;
}

int
vwmterm_ON_SCREEN_RESIZED(vwnd_t *vwnd, void *anything)
{
    if(anything != NULL)
    {
        if(strcmp((char *)anything, "fullscreen") == 0)
        {
            viper_wresize_abs(vwnd, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
        }
    }

    return 0;
}


int
vwmterm_ON_RESIZE(vwnd_t *vwnd, void *anything)
{
	vterm_t         *vterm;
    unsigned int    width;
    unsigned int    height;

	vterm = (vterm_t*)anything;

	getmaxyx(VWINDOW(vwnd), height, width);
    vterm_resize(vterm, width, height);
    vterm_wnd_update(vterm, -1, 0, 0);

	return 0;
}

int
vwmterm_ON_CLOSE(vwnd_t *vwnd, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
    pid_t           child_pid;

    (void)vwnd;

    vwmterm_data = (vwmterm_data_t*)anything;

    // tell the pseudo thread we're shutting down
    vwmterm_data->state = VWMTERM_STATE_EXITING;

    child_pid = vterm_get_pid(vwmterm_data->vterm);

	kill(child_pid, SIGKILL);
	waitpid(child_pid, NULL, 0);

	return 0;
}
