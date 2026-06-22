#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ncursesw/curses.h>

#include <vdk.h>
#include <vkmio.h>

#include "vwm.h"
#include "private.h"
#include "winman.h"
#include "panel.h"
#include "manage_windows.h"


#define DIALOG_WIDTH        48
#define DIALOG_HEIGHT       18
#define INTERIOR_WIDTH      (DIALOG_WIDTH - 2)
#define INTERIOR_HEIGHT     (DIALOG_HEIGHT - 2)

#define MANAGE_WINDOWS_HELP \
    "Tab cycles list / Close / Reorient / Cancel.  " \
    "Reorient moves the selected window to (1,1)."

#define WARNING_LINE_1  "Close the selected window?"
#define WARNING_LINE_2  "Any unsaved work in it will be lost."

enum
{
    BTN_CLOSE = 0,
    BTN_MOVE_TO,
    BTN_CANCEL,
    NUM_BUTTONS
};

enum
{
    FOCUS_LIST = 0,
    FOCUS_BTN_CLOSE,
    FOCUS_BTN_MOVE_TO,
    FOCUS_BTN_CANCEL,
    FOCUS_MAX
};

enum
{
    MOVE_FOCUS_LIST = 0,
    MOVE_FOCUS_OK,
    MOVE_FOCUS_CANCEL,
    MOVE_FOCUS_MAX
};


/* ── dialog widgets ────────────────────────────────────────── */

static vk_window_t          *dialog_window = NULL;
static vk_frame_t           *listbox_frame = NULL;
static vk_scroller_t        *listbox_scroller = NULL;
static vk_box_t             *main_vbox = NULL;
static vk_box_t             *button_hbox = NULL;
static vk_listbox_t         *windows_listbox = NULL;
static vk_button_t          *buttons[NUM_BUTTONS];

/* ── warning popup ─────────────────────────────────────────── */

static vk_popup_t           *warning_popup = NULL;
static vk_label_t           *warning_label_1 = NULL;
static vk_label_t           *warning_label_2 = NULL;
static vk_box_t             *warning_client = NULL;
static int                  warning_active_btn = 0;   /* 0 = Yes, 1 = No */

/* ── move-to picker popup ──────────────────────────────────── */

static vk_popup_t           *move_popup = NULL;
static vk_listbox_t         *move_listbox = NULL;
static vk_box_t             *move_client = NULL;
static int                  move_focus = MOVE_FOCUS_LIST;

/* ── small model ───────────────────────────────────────────── */

static int                  focus_zone = FOCUS_LIST;
static int                  list_count = 0;


/* ── forward decls ─────────────────────────────────────────── */

static void     build_dialog(void);
static void     refresh_dialog(void);
static void     rebuild_listbox(void);
static void     update_button_highlights(void);
static int      manage_windows_kmio(vk_object_t *object, int32_t keystroke);

static void     warning_popup_open(void);
static void     warning_popup_close(void);
static int      warning_popup_kmio(vk_object_t *object, int32_t keystroke);
static void     warning_update_buttons(void);

static void     move_popup_open(void);
static void     move_popup_close(void);
static int      move_popup_kmio(vk_object_t *object, int32_t keystroke);
static void     move_update_focus(void);
static void     move_apply(void);

static void     on_close(void);
static void     on_move_to(void);
static void     on_cancel(void);


/* ── listbox helpers ───────────────────────────────────────── */

static vk_widget_t*
get_selected_widget(void)
{
    vwm_t   *vwm;
    int     idx;

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->deck == NULL) return NULL;

    if(list_count == 0) return NULL;

    idx = vk_listbox_get_curr(windows_listbox);
    if(idx < 0 || idx >= list_count) return NULL;

    return vk_deck_get_widget(vwm->deck, idx);
}

static void
rebuild_listbox(void)
{
    vwm_t           *vwm;
    int             count;
    int             i;
    vk_widget_t     *w;
    const char      *title;
    char            label[INTERIOR_WIDTH];

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->deck == NULL)
    {
        count = 0;
    }
    else
    {
        count = vk_deck_count(vwm->deck);
    }

    vk_listbox_reset(windows_listbox);

    if(count == 0)
    {
        vk_listbox_add_item(windows_listbox, "None", NULL, NULL);
        list_count = 0;
        return;
    }

    for(i = 0; i < count; i++)
    {
        w = vk_deck_get_widget(vwm->deck, i);
        if(w == NULL) continue;

        title = vk_window_get_title(VK_WINDOW(w));
        if(title == NULL || title[0] == '\0')
            title = "(untitled)";

        snprintf(label, sizeof(label), "%s", title);
        vk_listbox_add_item(windows_listbox, label, NULL, NULL);
    }

    list_count = count;
}


/* ── focus/highlights ──────────────────────────────────────── */

static void
update_button_highlights(void)
{
    int i;

    for(i = 0; i < NUM_BUTTONS; i++)
    {
        bool focused = (focus_zone == FOCUS_BTN_CLOSE + i);
        vk_button_release(buttons[i]);
        vk_widget_set_colors(VK_WIDGET(buttons[i]),
            focused ? COLOR_YELLOW : COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(buttons[i]), A_BOLD);
        vk_button_update(buttons[i]);
    }

    vk_listbox_set_focused(windows_listbox, focus_zone == FOCUS_LIST);
}


/* ── refresh dialog ────────────────────────────────────────── */

static void
refresh_dialog(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    update_button_highlights();
    vk_listbox_update(windows_listbox);
    vk_scroller_update(listbox_scroller);
    vk_frame_update(listbox_frame);
    vk_box_update(button_hbox);
    vk_box_update(main_vbox);
    vk_window_update(dialog_window);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}


/* ── actions ───────────────────────────────────────────────── */

static void
on_close(void)
{
    vk_widget_t *w = get_selected_widget();
    if(w == NULL) return;            /* empty list or invalid selection */

    warning_popup_open();
}

static void
on_move_to(void)
{
    vk_widget_t *w = get_selected_widget();
    if(w == NULL) return;

    move_popup_open();
}

static void
on_cancel(void)
{
    vwm_manage_windows_close();
}

static void
do_close_selected(void)
{
    vk_widget_t *w = get_selected_widget();
    if(w == NULL) return;

    vwm_default_WINDOW_CLOSE(w);

    /*
        the listbox is now stale -- rebuild it from the new deck state
        and clamp the selection.
    */
    rebuild_listbox();

    if(list_count == 0)
    {
        vk_listbox_set_curr(windows_listbox, 0);
    }
    else
    {
        int curr = vk_listbox_get_curr(windows_listbox);
        if(curr >= list_count)
            vk_listbox_set_curr(windows_listbox, list_count - 1);
    }
}


/* ── warning popup ─────────────────────────────────────────── */

static void
warning_update_buttons(void)
{
    int i;

    if(warning_popup == NULL) return;

    for(i = 0; i < 2; i++)
    {
        vk_button_t *btn = vk_popup_get_button(warning_popup, i);
        if(btn == NULL) continue;

        vk_button_release(btn);
        vk_widget_set_colors(VK_WIDGET(btn),
            i == warning_active_btn ? COLOR_YELLOW : COLOR_BLACK,
            COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        vk_button_update(btn);
    }
}

static void
warning_popup_open(void)
{
    vwm_t       *vwm;
    vk_box_t    *button_bar;
    int         scr_w, scr_h;
    int         pw = 52;
    int         ph = 9;
    int         px, py;

    if(warning_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    warning_popup = vk_popup_create(pw, ph,
        VK_BORDER_SINGLE, "Yes", "No", NULL);
    vk_popup_set_title(warning_popup, " Confirm Close ");
    vk_popup_set_border_colors(warning_popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(warning_popup, A_NORMAL);

    button_bar = vk_popup_get_button_bar(warning_popup);
    if(button_bar != NULL)
    {
        vk_widget_set_colors(VK_WIDGET(button_bar), COLOR_RED, COLOR_WHITE);
        vk_widget_fill(VK_WIDGET(button_bar),
            ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    }

    warning_client = vk_box_create(pw - 2, ph - 5, VK_BOX_VERTICAL, 4);
    vk_box_set_homogeneous(warning_client, false);
    vk_widget_set_colors(VK_WIDGET(warning_client), COLOR_RED, COLOR_WHITE);

    {
        vk_filler_t *top_pad = vk_filler_create();
        vk_filler_t *bot_pad = vk_filler_create();

        vk_widget_set_colors(VK_WIDGET(top_pad), COLOR_RED, COLOR_WHITE);
        vk_widget_set_colors(VK_WIDGET(bot_pad), COLOR_RED, COLOR_WHITE);
        vk_widget_set_expand(VK_WIDGET(top_pad));
        vk_widget_set_expand(VK_WIDGET(bot_pad));

        warning_label_1 = vk_label_create(pw - 4);
        vk_label_set_justify(warning_label_1, VK_JUSTIFY_CENTER);
        vk_label_set_text(warning_label_1, WARNING_LINE_1);
        vk_widget_set_colors(VK_WIDGET(warning_label_1),
            COLOR_RED, COLOR_WHITE);
        vk_label_update(warning_label_1);

        warning_label_2 = vk_label_create(pw - 4);
        vk_label_set_justify(warning_label_2, VK_JUSTIFY_CENTER);
        vk_label_set_text(warning_label_2, WARNING_LINE_2);
        vk_widget_set_colors(VK_WIDGET(warning_label_2),
            COLOR_RED, COLOR_WHITE);
        vk_label_update(warning_label_2);

        vk_box_set_widget(warning_client, 0, VK_WIDGET(top_pad));
        vk_box_set_widget(warning_client, 1, VK_WIDGET(warning_label_1));
        vk_box_set_widget(warning_client, 2, VK_WIDGET(warning_label_2));
        vk_box_set_widget(warning_client, 3, VK_WIDGET(bot_pad));
    }

    vk_popup_set_client(warning_popup, VK_WIDGET(warning_client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(warning_client));
        vk_widget_set_state(VK_WIDGET(warning_client),
            st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(warning_popup), warning_popup_kmio);

    warning_active_btn = 1;     /* default to No -- destructive */

    px = (scr_w - pw) / 2;
    py = (scr_h - ph) / 2;
    if(px < 0) px = 0;
    if(py < 0) py = 0;
    vk_widget_move(VK_WIDGET(warning_popup), px, py);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(warning_popup));

    vk_widget_fill(VK_WIDGET(warning_client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(warning_client);
    warning_update_buttons();
    vk_popup_update(warning_popup);
    vk_screen_refresh(vwm->screen);
}

static void
warning_popup_close(void)
{
    vwm_t *vwm;

    if(warning_popup == NULL) return;

    vwm = vwm_get_instance();
    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(warning_popup));

    vk_window_destroy(VK_WINDOW(warning_popup));
    warning_popup = NULL;
    warning_client = NULL;
    warning_label_1 = NULL;
    warning_label_2 = NULL;
}

static int
warning_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        warning_popup_close();
        refresh_dialog();
        return 0;
    }

    if(keystroke == '\t' || keystroke == KEY_RIGHT || keystroke == KEY_LEFT
        || keystroke == KEY_BTAB)
    {
        warning_active_btn = (warning_active_btn == 0) ? 1 : 0;
        warning_update_buttons();
        vk_screen_refresh(vwm_get_instance()->screen);
        return 0;
    }

    if(keystroke == '\n' || keystroke == ' ' || keystroke == KEY_ENTER)
    {
        if(warning_active_btn == 0)
        {
            warning_popup_close();
            do_close_selected();
            refresh_dialog();
        }
        else
        {
            warning_popup_close();
            refresh_dialog();
        }
        return 0;
    }

    return 0;
}


/* ── move-to picker ────────────────────────────────────────── */

static void
move_update_focus(void)
{
    int i;

    if(move_popup == NULL) return;

    vk_listbox_set_focused(move_listbox, move_focus == MOVE_FOCUS_LIST);

    for(i = 0; i < 2; i++)
    {
        vk_button_t *btn = vk_popup_get_button(move_popup, i);
        bool focused = (move_focus == MOVE_FOCUS_OK + i);

        if(btn == NULL) continue;

        vk_button_release(btn);
        vk_widget_set_colors(VK_WIDGET(btn),
            focused ? COLOR_YELLOW : COLOR_WHITE, COLOR_BLUE);
        vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        vk_button_update(btn);
    }

    vk_listbox_update(move_listbox);
    vk_box_update(move_client);
    vk_popup_update(move_popup);
    vk_screen_refresh(vwm_get_instance()->screen);
}

static void
move_apply(void)
{
    vwm_t       *vwm;
    vk_widget_t *w;
    int         idx;
    int         active_surface;
    int         target_surface;

    /*
        move_listbox must still be live when this runs.  callers must
        invoke move_apply BEFORE move_popup_close, since the close
        destroys the popup (and its listbox) we read the selection from.
    */
    if(move_listbox == NULL) return;

    w = get_selected_widget();
    if(w == NULL) return;

    vwm = vwm_get_instance();

    idx = vk_listbox_get_curr(move_listbox);

    if(idx == 0)
    {
        /* Home coordinates -- move to (1, 1) on the current desktop */
        vk_widget_move(w, 1, 1);
        vk_window_update(VK_WINDOW(w));
        vk_screen_refresh(vwm->screen);
        return;
    }

    /*
        Desktop N options run from index 1 upward.  "Desktop K" maps to
        surface index K - 1.
    */
    target_surface = idx - 1;
    active_surface = vk_screen_get_active_surface(vwm->screen);

    if(target_surface == active_surface) return;          /* no-op */
    if(target_surface < 0
        || target_surface >= vwm->surface_count) return;

    vk_deck_remove_widget(vwm->decks[active_surface], w);
    vk_deck_add_widget(vwm->decks[target_surface], w, VK_DECK_TOP);

    /*
        the listbox in the main dialog is now stale (one fewer window
        in the active deck).  rebuild it and reset selection.
    */
    rebuild_listbox();
    if(list_count == 0)
    {
        vk_listbox_set_curr(windows_listbox, 0);
    }
    else
    {
        int curr = vk_listbox_get_curr(windows_listbox);
        if(curr >= list_count)
            vk_listbox_set_curr(windows_listbox, list_count - 1);
    }

    vk_screen_refresh(vwm->screen);
}

static void
move_popup_open(void)
{
    vwm_t       *vwm;
    vk_box_t    *button_bar;
    int         scr_w, scr_h;
    int         pw = 36;
    int         ph;
    int         lb_height;
    int         desktops;
    int         i;
    int         px, py;
    char        label[32];

    if(move_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    desktops = vwm->surface_count;

    /*
        always 1 row for "Home coordinates", plus 1 row per desktop when
        there is more than one.  cap the listbox at 6 rows so we never
        run past the popup height (VWM_MAX_DESKTOPS == 6, "Home" + 6 = 7).
    */
    lb_height = 1 + (desktops > 1 ? desktops : 0);
    if(lb_height > 7) lb_height = 7;

    ph = lb_height + 5;     /* +5 = borders (2) + button bar (3) */

    move_popup = vk_popup_create(pw, ph,
        VK_BORDER_SINGLE, "Okay", "Cancel", NULL);
    vk_popup_set_title(move_popup, " Move To... ");
    vk_popup_set_border_colors(move_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(move_popup, A_BOLD);

    button_bar = vk_popup_get_button_bar(move_popup);
    if(button_bar != NULL)
    {
        vk_widget_set_colors(VK_WIDGET(button_bar), COLOR_WHITE, COLOR_BLUE);
        vk_widget_fill(VK_WIDGET(button_bar),
            ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
    }

    move_client = vk_box_create(pw - 2, lb_height, VK_BOX_VERTICAL, 1);
    vk_widget_set_colors(VK_WIDGET(move_client), COLOR_WHITE, COLOR_BLUE);

    move_listbox = vk_listbox_create(pw - 4, lb_height);
    vk_widget_set_colors(VK_WIDGET(move_listbox), COLOR_WHITE, COLOR_BLUE);
    vk_widget_set_attrs(VK_WIDGET(move_listbox), A_BOLD);
    vk_listbox_set_highlight(move_listbox, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_unfocused(move_listbox, COLOR_BLACK, COLOR_WHITE);
    vk_listbox_set_wrap(move_listbox, FALSE);

    vk_listbox_add_item(move_listbox, "Home coordinates", NULL, NULL);
    if(desktops > 1)
    {
        for(i = 0; i < desktops; i++)
        {
            snprintf(label, sizeof(label), "Desktop %d", i + 1);
            vk_listbox_add_item(move_listbox, label, NULL, NULL);
        }
    }
    vk_listbox_set_curr(move_listbox, 0);

    vk_box_set_widget(move_client, 0, VK_WIDGET(move_listbox));

    vk_popup_set_client(move_popup, VK_WIDGET(move_client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(move_client));
        vk_widget_set_state(VK_WIDGET(move_client),
            st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(move_popup), move_popup_kmio);

    move_focus = MOVE_FOCUS_LIST;

    px = (scr_w - pw) / 2;
    py = (scr_h - ph) / 2;
    if(px < 0) px = 0;
    if(py < 0) py = 0;
    vk_widget_move(VK_WIDGET(move_popup), px, py);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(move_popup));

    vk_widget_fill(VK_WIDGET(move_client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
    vk_box_update(move_client);
    move_update_focus();
}

static void
move_popup_close(void)
{
    vwm_t *vwm;

    if(move_popup == NULL) return;

    vwm = vwm_get_instance();
    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(move_popup));

    vk_window_destroy(VK_WINDOW(move_popup));
    move_popup = NULL;
    move_client = NULL;
    move_listbox = NULL;
}

static int
move_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        move_popup_close();
        refresh_dialog();
        return 0;
    }

    if(keystroke == '\t')
    {
        move_focus++;
        if(move_focus >= MOVE_FOCUS_MAX) move_focus = MOVE_FOCUS_LIST;
        move_update_focus();
        return 0;
    }

    if(keystroke == KEY_BTAB)
    {
        move_focus--;
        if(move_focus < 0) move_focus = MOVE_FOCUS_MAX - 1;
        move_update_focus();
        return 0;
    }

    if(move_focus == MOVE_FOCUS_LIST)
    {
        vk_object_push_keystroke(VK_OBJECT(move_listbox), keystroke);
        move_update_focus();
        return 0;
    }

    if(keystroke == '\n' || keystroke == ' ' || keystroke == KEY_ENTER)
    {
        if(move_focus == MOVE_FOCUS_OK)
        {
            /* apply must happen before close -- close frees move_listbox */
            move_apply();
        }
        move_popup_close();
        refresh_dialog();
        return 0;
    }

    return 0;
}


/* ── handle button keystrokes ──────────────────────────────── */

static void
press_focused_button(void)
{
    switch(focus_zone)
    {
        case FOCUS_BTN_CLOSE:    on_close();   break;
        case FOCUS_BTN_MOVE_TO:  on_move_to(); break;
        case FOCUS_BTN_CANCEL:   on_cancel();  break;
    }
}


/* ── main kmio ─────────────────────────────────────────────── */

static int
manage_windows_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    /*
        when vwm->tool_window is set, poll_input_thd routes every
        keystroke -- KEY_MOUSE included -- through this kmio.
    */
    if(keystroke == KEY_MOUSE)
    {
        vwm_manage_windows_mouse(vk_kmio_get_mouse_event());
        return 0;
    }

    if(warning_popup != NULL)
        return warning_popup_kmio(NULL, keystroke);

    if(move_popup != NULL)
        return move_popup_kmio(NULL, keystroke);

    if(keystroke == 27)
    {
        on_cancel();
        return 0;
    }

    if(keystroke == '\t')
    {
        focus_zone++;
        if(focus_zone >= FOCUS_MAX) focus_zone = FOCUS_LIST;
        refresh_dialog();
        return 0;
    }

    if(keystroke == KEY_BTAB)
    {
        focus_zone--;
        if(focus_zone < 0) focus_zone = FOCUS_MAX - 1;
        refresh_dialog();
        return 0;
    }

    if(focus_zone == FOCUS_LIST)
    {
        if(list_count == 0)
        {
            /* nothing in the deck; nudge to the buttons */
            return 0;
        }
        vk_object_push_keystroke(VK_OBJECT(windows_listbox), keystroke);
        refresh_dialog();
        return 0;
    }

    if(keystroke == '\n' || keystroke == ' ' || keystroke == KEY_ENTER)
    {
        press_focused_button();
        return 0;
    }

    return 0;
}


/* ── build dialog ──────────────────────────────────────────── */

static void
build_dialog(void)
{
    vwm_t       *vwm;
    vk_filler_t *button_spacer;
    int         scr_w, scr_h;
    int         lb_height;
    int         pos_x, pos_y;
    int         i;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    dialog_window = vk_window_create(DIALOG_WIDTH, DIALOG_HEIGHT);
    vk_window_set_title(dialog_window, " Manage Desktop ");
    vk_window_set_border_style(dialog_window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(dialog_window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(dialog_window, A_BOLD);

    {
        uint32_t state = vk_widget_get_state(VK_WIDGET(dialog_window));
        vk_widget_set_state(VK_WIDGET(dialog_window),
            state | VK_STATE_NORESIZE);
    }

    main_vbox = vk_box_create(INTERIOR_WIDTH, INTERIOR_HEIGHT,
        VK_BOX_VERTICAL, 2);
    vk_box_set_homogeneous(main_vbox, false);
    vk_widget_set_colors(VK_WIDGET(main_vbox), COLOR_BLACK, COLOR_CYAN);

    lb_height = INTERIOR_HEIGHT - 3 - 2;

    windows_listbox = vk_listbox_create(INTERIOR_WIDTH - 2, lb_height);
    vk_listbox_set_wrap(windows_listbox, FALSE);
    vk_listbox_set_highlight(windows_listbox, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_unfocused(windows_listbox, COLOR_BLACK, COLOR_WHITE);
    vk_widget_set_colors(VK_WIDGET(windows_listbox),
        COLOR_BLACK, COLOR_CYAN);

    listbox_frame = vk_frame_create(INTERIOR_WIDTH, lb_height + 2);
    vk_frame_set_border_style(listbox_frame,
        VK_BORDER_SINGLE | VK_RELIEF_SUNKEN);
    vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
    vk_frame_set_border_attrs(listbox_frame, A_BOLD);
    vk_frame_set_child(listbox_frame, VK_WIDGET(windows_listbox));
    vk_widget_set_expand(VK_WIDGET(listbox_frame));

    listbox_scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
    vk_scroller_set_border_style(listbox_scroller, VK_BORDER_SINGLE);
    vk_scroller_set_border_colors(listbox_scroller,
        COLOR_BLACK, COLOR_CYAN);
    vk_scroller_set_scroll_source(listbox_scroller,
        VK_WIDGET(windows_listbox));
    vk_widget_attach_scroller(VK_WIDGET(windows_listbox),
        listbox_scroller);

    button_hbox = vk_box_create(INTERIOR_WIDTH, 3,
        VK_BOX_HORIZONTAL, 4);
    vk_box_set_homogeneous(button_hbox, false);
    vk_widget_set_colors(VK_WIDGET(button_hbox), COLOR_BLACK, COLOR_CYAN);

    buttons[BTN_CLOSE]    = vk_button_create("Close Window");
    buttons[BTN_MOVE_TO]  = vk_button_create("Move Window");
    buttons[BTN_CANCEL]   = vk_button_create("Cancel");

    for(i = 0; i < NUM_BUTTONS; i++)
    {
        vk_button_set_relief_style(buttons[i], VK_BORDER_SINGLE);
        vk_widget_set_colors(VK_WIDGET(buttons[i]),
            COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_attrs(VK_WIDGET(buttons[i]), A_BOLD);
        vk_button_set_pressed_colors(buttons[i],
            COLOR_WHITE, COLOR_BLUE);
    }

    button_spacer = vk_filler_create();
    vk_widget_set_colors(VK_WIDGET(button_spacer), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_expand(VK_WIDGET(button_spacer));

    vk_box_set_widget(button_hbox, 0, VK_WIDGET(buttons[BTN_CLOSE]));
    vk_box_set_widget(button_hbox, 1, VK_WIDGET(buttons[BTN_MOVE_TO]));
    vk_box_set_widget(button_hbox, 2, VK_WIDGET(button_spacer));
    vk_box_set_widget(button_hbox, 3, VK_WIDGET(buttons[BTN_CANCEL]));

    vk_box_set_widget(main_vbox, 0, VK_WIDGET(listbox_frame));
    vk_box_set_widget(main_vbox, 1, VK_WIDGET(button_hbox));

    vk_window_set_child(dialog_window, VK_WIDGET(main_vbox));
    vk_object_set_kmio(VK_OBJECT(dialog_window), manage_windows_kmio);

    rebuild_listbox();
    vk_listbox_set_curr(windows_listbox, 0);

    focus_zone = FOCUS_LIST;
    update_button_highlights();

    pos_x = (scr_w - DIALOG_WIDTH) / 2;
    pos_y = (scr_h - DIALOG_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;
    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vwm->tool_window = dialog_window;

    refresh_dialog();
}


/* ── public API ────────────────────────────────────────────── */

int
vwm_manage_windows_open(vk_widget_t *widget, void *anything)
{
    vwm_t *vwm;

    (void)widget;
    (void)anything;

    if(dialog_window != NULL) return -1;

    vwm = vwm_get_instance();

    {
        int scr_w, scr_h;
        getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

        if(scr_w < DIALOG_WIDTH || scr_h < DIALOG_HEIGHT)
        {
            vwm_panel_set_status(
                "Terminal too small for Manage Desktop");
            return -1;
        }
    }

    build_dialog();

    vwm_panel_set_status(MANAGE_WINDOWS_HELP);

    return 0;
}

void
vwm_manage_windows_close(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    if(warning_popup != NULL)
        warning_popup_close();

    if(move_popup != NULL)
        move_popup_close();

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vk_window_destroy(dialog_window);
    dialog_window = NULL;
    main_vbox = NULL;
    button_hbox = NULL;
    listbox_frame = NULL;
    listbox_scroller = NULL;
    windows_listbox = NULL;
    memset(buttons, 0, sizeof(buttons));

    vwm->tool_window = NULL;

    {
        vk_widget_t *top = vk_deck_get_top(vwm->deck);
        if(top != NULL)
            vwm_panel_set_status(VWM_WINDOW_HELP);
        else
            vwm_panel_set_status("Press Alt ~ for Menu");
    }

    vk_screen_refresh(vwm->screen);
}

bool
vwm_manage_windows_is_open(void)
{
    return (dialog_window != NULL);
}

vk_widget_t*
vwm_manage_windows_get_warning_popup(void)
{
    return VK_WIDGET(warning_popup);
}


/* ── resize ────────────────────────────────────────────────── */

void
vwm_manage_windows_handle_resize(void)
{
    if(dialog_window == NULL) return;

    /*
        rebuild the whole dialog so it re-centers and picks up the new
        screen geometry.  matches the pattern other system tools use on
        KEY_RESIZE.
    */
    vwm_manage_windows_close();
    vwm_manage_windows_open(NULL, NULL);
}


/* ── mouse ─────────────────────────────────────────────────── */

int
vwm_manage_windows_mouse(MEVENT *mouse_event)
{
    int wx, wy, ww, wh;
    int rel_x, rel_y;

    if(dialog_window == NULL || mouse_event == NULL) return 0;

    if(move_popup != NULL)
    {
        int pw, ph, px, py;
        int interior_x;
        vk_widget_get_position(VK_WIDGET(move_popup), &px, &py);
        vk_widget_get_metrics(VK_WIDGET(move_popup), &pw, &ph);

        if(mouse_event->x < px || mouse_event->x >= px + pw
            || mouse_event->y < py || mouse_event->y >= py + ph)
        {
            return 0;
        }

        if(!(mouse_event->bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            return 0;

        rel_x = mouse_event->x - px;
        rel_y = mouse_event->y - py;
        interior_x = rel_x - 1;

        /* button-bar row is the last 3 rows of the popup */
        if(rel_y >= ph - 3)
        {
            int mid = (pw - 2) / 2;

            if(interior_x < mid)
            {
                move_focus = MOVE_FOCUS_OK;
                move_update_focus();
                /* apply must happen before close -- close frees move_listbox */
                move_apply();
            }
            move_popup_close();
            refresh_dialog();
            return 0;
        }

        /* listbox area */
        {
            int lb_row = rel_y - 1;     /* skip top border */
            int item;

            if(lb_row < 0) return 0;
            item = vk_listbox_get_scroll_pos(move_listbox) + lb_row;
            if(item >= 0 && item < vk_listbox_get_item_count(move_listbox))
                vk_listbox_set_curr(move_listbox, item);

            move_focus = MOVE_FOCUS_LIST;
            move_update_focus();
        }

        return 0;
    }

    if(warning_popup != NULL)
    {
        /*
            warning popup is modal -- intercept clicks landing on it.
            outside clicks are no-ops while it's open.
        */
        int pw, ph, px, py;
        vk_widget_get_position(VK_WIDGET(warning_popup), &px, &py);
        vk_widget_get_metrics(VK_WIDGET(warning_popup), &pw, &ph);

        if(mouse_event->x < px || mouse_event->x >= px + pw
            || mouse_event->y < py || mouse_event->y >= py + ph)
        {
            return 0;
        }

        if(!(mouse_event->bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED)))
            return 0;

        rel_x = mouse_event->x - px;
        rel_y = mouse_event->y - py;
        (void)rel_y;

        /* Yes is the left button, No is the right -- in the centered bar */
        if(rel_x < pw / 2)
            warning_active_btn = 0;
        else
            warning_active_btn = 1;

        if(warning_active_btn == 0)
        {
            warning_popup_close();
            do_close_selected();
        }
        else
        {
            warning_popup_close();
        }
        refresh_dialog();
        return 0;
    }

    vk_widget_get_position(VK_WIDGET(dialog_window), &wx, &wy);
    vk_widget_get_metrics(VK_WIDGET(dialog_window), &ww, &wh);

    rel_x = mouse_event->x - wx;
    rel_y = mouse_event->y - wy;

    if(rel_x < 0 || rel_x >= ww || rel_y < 0 || rel_y >= wh)
    {
        /* clicked outside the dialog -- ignore.  It no longer dismisses
           the dialog; close it with the Close/Cancel button or Esc. */
        return 0;
    }

    /*
        coarse hit-test: the listbox area is the top portion, the button
        bar is the bottom 3 rows.  the dialog interior starts at row 1
        (skipping the border).
    */
    if(rel_y >= wh - 4)
    {
        /* button bar:  Close | Reorient | <spacer> | Cancel */
        int interior_x = rel_x - 1;     /* skip left border */

        if(!(mouse_event->bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED)))
            return 0;

        if(interior_x < 14)
        {
            focus_zone = FOCUS_BTN_CLOSE;
            update_button_highlights();
            on_close();
        }
        else if(interior_x < 27)
        {
            focus_zone = FOCUS_BTN_MOVE_TO;
            update_button_highlights();
            on_move_to();
        }
        else if(interior_x >= INTERIOR_WIDTH - 8)
        {
            focus_zone = FOCUS_BTN_CANCEL;
            update_button_highlights();
            on_cancel();
        }
        return 0;
    }

    /*
        listbox area: compute the clicked item from rel_y.  the listbox
        starts at row 1 (top border of the dialog) + 1 (sunken-relief
        frame top border) = row 2.  add the listbox's current scroll
        position to get the absolute item index.
    */
    if(list_count == 0) return 0;

    focus_zone = FOCUS_LIST;
    update_button_highlights();

    if(mouse_event->bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))
    {
        int lb_row = rel_y - 2;
        if(lb_row >= 0)
        {
            int item = vk_listbox_get_scroll_pos(windows_listbox) + lb_row;
            if(item >= 0 && item < list_count)
                vk_listbox_set_curr(windows_listbox, item);
        }
    }

    refresh_dialog();

    return 0;
}
