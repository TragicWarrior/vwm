#include <ncursesw/curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>

#include <vdk.h>
#include <vkmio.h>

#include "vwm.h"
#include "private.h"
#include "modules.h"
#include "strings.h"
#include "manage_ui_common.h"
#include "launch_picker.h"
#include "winman.h"

/*
    A standalone modal file picker for the "%fd" launch token.  It reuses
    the shared load-popup construction (vwm_load_popup_show) and rides the
    vwm->tool_window slot so poll_input feeds it all input -- its kmio
    drives the filedialog for both keyboard and mouse, mirroring the print
    module's pick session.
*/

static vk_popup_t       *picker;
static vk_filedialog_t  *picker_fd;
static vwm_module_t     *picker_module;

static struct timespec  last_click_time;
static int              last_click_item = -1;
static int              picker_focus = VWM_LOAD_FOCUS_FILEDIALOG;

/* Return a newly-allocated copy of `src` with every "%fd" replaced by
   `repl`, or NULL if `src` holds no "%fd".  Substitution is per argv
   element (after the space-split), so a chosen path that contains spaces
   stays a single argument. */
static char *
str_replace_fd(const char *src, const char *repl)
{
    const char  *token = "%fd";
    const size_t tlen = 3;
    size_t      rlen = strlen(repl);
    const char  *p;
    const char  *cur;
    char        *out;
    char        *dst;
    int         count = 0;

    for(p = src; (p = strstr(p, token)) != NULL; p += tlen) count++;
    if(count == 0) return NULL;

    /* over-allocate: src length plus the replacement bytes is always
       enough, since each match drops tlen and adds rlen */
    out = malloc(strlen(src) + (size_t)count * rlen + 1);
    if(out == NULL) return NULL;

    dst = out;
    cur = src;
    while((p = strstr(cur, token)) != NULL)
    {
        size_t n = (size_t)(p - cur);
        memcpy(dst, cur, n);
        dst += n;
        memcpy(dst, repl, rlen);
        dst += rlen;
        cur = p + tlen;
    }
    strcpy(dst, cur);

    return out;
}

static bool
picker_double_click(int item)
{
    struct timespec now;
    bool            dbl = false;

    clock_gettime(CLOCK_MONOTONIC, &now);

    if(item == last_click_item)
    {
        long ms = (now.tv_sec - last_click_time.tv_sec) * 1000
            + (now.tv_nsec - last_click_time.tv_nsec) / 1000000;
        if(ms >= 0 && ms < 400) dbl = true;
    }

    last_click_time = now;
    last_click_item = item;

    return dbl;
}

static void
launch_picker_redraw(void)
{
    vwm_t *vwm = vwm_get_instance();

    if(picker == NULL) return;

    vk_filedialog_update(picker_fd);
    vk_popup_update(picker);
    vk_screen_refresh(vwm->screen);
}

static void
launch_picker_close(void)
{
    vwm_t *vwm = vwm_get_instance();

    if(picker == NULL) return;

    /* release the modal slot before tearing the widget down */
    vwm->tool_window = NULL;

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), VK_WIDGET(picker));
    vk_popup_destroy(picker);

    picker = NULL;
    picker_fd = NULL;
    picker_module = NULL;

    vk_screen_refresh(vwm->screen);
}

/* Substitute the chosen file into a copy of the program's argv, re-arm
   the module, tear the picker down, and launch. */
static void
launch_picker_launch(const char *fullpath)
{
    vwm_t        *vwm = vwm_get_instance();
    vwm_module_t *module = picker_module;
    char         **argv;
    vk_window_t  *window;
    int          i;

    if(module == NULL || module->fd_argv == NULL)
    {
        launch_picker_close();
        return;
    }

    argv = strdupv(module->fd_argv, 0);
    for(i = 0; argv[i] != NULL; i++)
    {
        char *sub = str_replace_fd(argv[i], fullpath);
        if(sub != NULL)
        {
            free(argv[i]);
            argv[i] = sub;
        }
    }

    /* re-arm the module with the substituted args (argv[0] is the bin);
       vwmterm_module_configure dups them and frees the previous set */
    vwm_module_configure(module, argv[0], argv);
    strfreev(argv);

    /* tear the picker down before spawning the program's window so the
       new vwmterm becomes the deck top, not a modal overlay */
    launch_picker_close();

    window = module->main(module);
    if(window != NULL)
    {
        vwm_deck_add_window(vwm->deck, VK_WIDGET(window), VK_DECK_TOP);
        vk_screen_refresh(vwm->screen);
    }
}

/* Act on the current filedialog selection: a directory navigates, a file
   launches. */
static void
launch_picker_activate(void)
{
    const char  *path;
    const char  *selected;
    char        fullpath[PATH_MAX];
    int         len;

    if(picker_fd == NULL) return;

    selected = vk_filedialog_get_selected(picker_fd);
    if(selected == NULL || selected[0] == '\0') return;

    len = (int)strlen(selected);

    if(selected[len - 1] == '/' || strcmp(selected, "..") == 0)
    {
        /* a directory -> navigate into it instead of launching */
        vk_object_push_keystroke(VK_OBJECT(picker_fd), KEY_CRLF);
        launch_picker_redraw();
        return;
    }

    path = vk_filedialog_get_path(picker_fd);
    if(path == NULL) return;

    if(strcmp(path, "/") == 0)
        snprintf(fullpath, sizeof(fullpath), "/%s", selected);
    else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, selected);

    launch_picker_launch(fullpath);
}

static void
launch_picker_mouse(MEVENT *me)
{
    int          wx, wy, ww, wh, ix, iy;
    int          fd_h, interior_w;
    mmask_t      bs;
    vk_listbox_t *fl;

    if(me == NULL || picker == NULL) return;

    bs = me->bstate;

    vk_widget_get_position(VK_WIDGET(picker), &wx, &wy);
    vk_widget_get_metrics(VK_WIDGET(picker), &ww, &wh);

    ix = me->x - wx - 1;
    iy = me->y - wy - 1;

    /* off the dialog -- ignore.  A press off the window no longer aborts
       the launch; use Cancel or Esc to dismiss the picker. */
    if(ix < 0 || ix >= ww - 2 || iy < 0 || iy >= wh - 2)
    {
        return;
    }

    fd_h = wh - 2;
    interior_w = ww - 2;

    /* path strip (top 3 rows) -> open the filedialog's path entry */
    if(iy < 3)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            vk_object_push_keystroke(VK_OBJECT(picker_fd), '/');
            launch_picker_redraw();
        }
        return;
    }

    /* OK / Cancel bar (bottom 3 rows): left half launches, right aborts */
    if(iy >= fd_h - 3)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            if(ix < interior_w / 2)
                launch_picker_activate();
            else
                launch_picker_close();
        }
        return;
    }

    /* file list */
    fl = vk_filedialog_get_file_list(picker_fd);
    if(fl == NULL) return;

    if(bs & BUTTON4_PRESSED) { vk_listbox_set_prev(fl); launch_picker_redraw(); return; }
    if(bs & BUTTON5_PRESSED) { vk_listbox_set_next(fl); launch_picker_redraw(); return; }

    if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
    {
        int scroll = vk_listbox_get_scroll_pos(fl);
        /* iy-3 skips the 3-row path strip; -1 more skips the frame's top */
        int clicked = scroll + (iy - 3 - 1);
        int count = vk_listbox_get_item_count(fl);

        if(clicked < 0 || clicked >= count) return;

        vk_listbox_set_curr(fl, clicked);
        picker_focus = VWM_LOAD_FOCUS_FILEDIALOG;
        vwm_load_popup_paint_focus(picker_fd, picker_focus);
        launch_picker_redraw();

        if(picker_double_click(clicked)) launch_picker_activate();
    }
}

static int
launch_picker_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(picker == NULL) return 0;

    if(keystroke == KEY_MOUSE)
    {
        launch_picker_mouse(vk_kmio_get_mouse_event());
        return 0;
    }

    if(keystroke == 27)             /* Esc -> abort the launch */
    {
        launch_picker_close();
        return 0;
    }

    /* Tab cycles focus: file browser -> Okay -> Cancel (matches the
       Hotkeys / Apps load dialogs and the print pick session) */
    if(keystroke == '\t')
    {
        picker_focus = (picker_focus + 1) % VWM_LOAD_FOCUS_COUNT;
        vwm_load_popup_paint_focus(picker_fd, picker_focus);
        launch_picker_redraw();
        return 0;
    }

    if(picker_focus == VWM_LOAD_FOCUS_OK)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            launch_picker_activate();
        return 0;
    }

    if(picker_focus == VWM_LOAD_FOCUS_CANCEL)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            launch_picker_close();
        return 0;
    }

    /* VWM_LOAD_FOCUS_FILEDIALOG: drive the file browser */
    if(keystroke == KEY_CRLF)
    {
        const char *sel = vk_filedialog_get_selected(picker_fd);
        if(sel != NULL && sel[0] != '\0')
        {
            int len = (int)strlen(sel);
            if(sel[len - 1] != '/' && strcmp(sel, "..") != 0)
            {
                launch_picker_activate();
                return 0;
            }
        }
        /* a directory: fall through so the filedialog navigates into it */
    }

    vk_object_push_keystroke(VK_OBJECT(picker_fd), keystroke);
    launch_picker_redraw();
    return 0;
}

void
vwm_launch_picker_open(vwm_module_t *module)
{
    vwm_t       *vwm;
    char        seed[PATH_MAX];
    const char  *home;

    if(module == NULL || module->fd_argv == NULL) return;

    vwm = vwm_get_instance();

    /* one modal tool at a time (the picker shares the tool_window slot) */
    if(vwm->tool_window != NULL || picker != NULL) return;

    home = getenv("HOME");
    if(home == NULL || home[0] == '\0') home = "/";

    /* vwm_load_popup_show roots the browser at the *directory of* its
       cur_path argument, so hand it a dummy child of $HOME to land in
       $HOME itself */
    snprintf(seed, sizeof(seed), "%s/.", home);

    picker = vwm_load_popup_show(" Select a File ", &picker_fd, seed);
    if(picker == NULL) return;

    picker_module = module;
    last_click_item = -1;
    picker_focus = VWM_LOAD_FOCUS_FILEDIALOG;

    vk_object_set_kmio(VK_OBJECT(picker), launch_picker_kmio);
    vwm_load_popup_paint_focus(picker_fd, picker_focus);

    /* park the popup in the modal tool_window slot.  tool_window is only
       ever accessed via VK_OBJECT (poll_input pushes all input to its
       kmio), so a vk_popup is safe here -- nothing calls a
       vk_window-specific function on it. */
    vwm->tool_window = (vk_window_t *)picker;

    vk_popup_update(picker);
    vk_screen_refresh(vwm->screen);
}
