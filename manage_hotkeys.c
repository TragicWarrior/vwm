#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include <ncursesw/curses.h>

#include <vdk.h>

#include "cJSON.h"
#include "config.h"
#include "vwm.h"
#include "private.h"
#include "profile.h"
#include "settings.h"
#include "panel.h"
#include "winman.h"
#include "mainmenu.h"
#include "manage_hotkeys.h"
#include "manage_ui_common.h"

#define MANAGE_HOTKEYS_HELP \
"[Tab] cycle controls  [Up/Dn] select hotkey  " \
"[Test] capture key  [Reset] restore default"

#define DIALOG_WIDTH        60
#define DIALOG_HEIGHT       22
#define INTERIOR_WIDTH      (DIALOG_WIDTH - 2)
#define INTERIOR_HEIGHT     (DIALOG_HEIGHT - 2)

#define NUM_HOTKEYS         VWM_HOTKEY_COUNT
#define ITEM_WIDTH          (INTERIOR_WIDTH - 4)

enum
{
    CAT_MENU = 0,
    CAT_WM,
    CAT_NAV
};

/*
    What the dialog calls each hotkey, and the group it is listed under.
    The config name, the offset into vwm_t and the default live in
    vwm_hotkeys, in this same order.
*/
static const struct
{
    const char  *label;
    int         category;
}
hotkey_ui[] =
{
    { "Main Menu",       CAT_MENU },
    { "Toggle WM",       CAT_WM },
    { "Close Window",    CAT_WM },
    { "Cycle Windows",   CAT_WM },
    { "Move Up",         CAT_WM },
    { "Move Down",       CAT_WM },
    { "Move Left",       CAT_WM },
    { "Move Right",      CAT_WM },
    { "Increase Height", CAT_WM },
    { "Decrease Height", CAT_WM },
    { "Increase Width",  CAT_WM },
    { "Decrease Width",  CAT_WM },
    { "Switch Desktop",  CAT_NAV },
    { "Detach",          CAT_NAV },
};

/* the dialog rows and vwm_hotkeys are the same list */
_Static_assert(
    sizeof(hotkey_ui) / sizeof(hotkey_ui[0]) == VWM_HOTKEY_COUNT,
    "hotkey dialog rows and vwm_hotkeys");

enum
{
    FOCUS_HOTKEY_LIST = 0,
    FOCUS_BTN_MODIFY,
    FOCUS_BTN_RESET,
    FOCUS_BTN_SAVE,
    FOCUS_BTN_LOAD,
    FOCUS_BTN_CLOSE,
    FOCUS_MAX
};

#define NUM_BUTTONS     5
#define BTN_MODIFY        0
#define BTN_RESET       1
#define BTN_SAVE        2
#define BTN_LOAD        3
#define BTN_CLOSE      4

typedef struct
{
    int32_t     values[NUM_HOTKEYS];
    int         selected;
    int         focus_zone;
    char        file_path[PATH_MAX];
    bool        capture_mode;
}
hotkey_model_t;

static hotkey_model_t       *model = NULL;
static vk_window_t          *dialog_window = NULL;
static vk_frame_t           *listbox_frame = NULL;
static vk_scroller_t        *listbox_scroller = NULL;
static vk_box_t             *main_vbox = NULL;
static vk_box_t             *button_hbox = NULL;
static vk_listbox_t         *hotkey_listbox = NULL;
static vk_button_t          *buttons[NUM_BUTTONS];

static int                  entry_to_lb[NUM_HOTKEYS];

static struct timespec      list_last_click_time;
static int                  list_last_click_item = -1;

static vk_popup_t           *load_popup = NULL;
static vk_filedialog_t      *load_filedialog = NULL;
static struct timespec       load_last_click_time;
static int                   load_last_click_item = -1;

/* Tab-stop focus inside the Load Config popup */
enum
{
    HL_FILEDIALOG = 0,
    HL_OKAY,
    HL_CANCEL,
    HL_COUNT
};
static int                   hk_load_focus = HL_FILEDIALOG;

/* these values are what the shared load controller switches on */
_Static_assert((int)HL_FILEDIALOG == (int)VWM_LOAD_FOCUS_FILEDIALOG,
    "hotkeys load focus");
_Static_assert((int)HL_OKAY == (int)VWM_LOAD_FOCUS_OK, "hotkeys load focus");
_Static_assert((int)HL_CANCEL == (int)VWM_LOAD_FOCUS_CANCEL,
    "hotkeys load focus");

static vk_popup_t           *error_popup = NULL;
static vk_popup_t           *confirm_popup = NULL;
static vk_popup_t           *warning_popup = NULL;
static vk_popup_t           *saved_popup = NULL;
static int                  confirm_active_btn = 0;

static void rebuild_listbox(void);
static void refresh_dialog(void);
static void refresh_load_popup(void);
static int  manage_hotkeys_kmio(vk_object_t *object, int32_t keystroke);

/* ── hotkey display formatting ─────────────────────────────── */

static void
hotkey_format(int32_t keycode, char *buf, int buflen)
{
    if((keycode & 0xFF) == 27 && (keycode >> 8) != 0)
    {
        char c = (keycode >> 8) & 0xFF;

        if(c == '`')
            snprintf(buf, buflen, "Alt+~");
        else
            snprintf(buf, buflen, "Alt+%c", c);
        return;
    }

    if(keycode == 9)
    {
        snprintf(buf, buflen, "Tab");
        return;
    }

    if(keycode == 10)
    {
        snprintf(buf, buflen, "Enter");
        return;
    }

    if(keycode == 27)
    {
        snprintf(buf, buflen, "Esc");
        return;
    }

    if(keycode >= 1 && keycode <= 26)
    {
        snprintf(buf, buflen, "Ctrl+%c", 'A' + keycode - 1);
        return;
    }

    switch(keycode)
    {
        case KEY_UP:    snprintf(buf, buflen, "Up");        return;
        case KEY_DOWN:  snprintf(buf, buflen, "Down");      return;
        case KEY_LEFT:  snprintf(buf, buflen, "Left");      return;
        case KEY_RIGHT: snprintf(buf, buflen, "Right");     return;
        case KEY_HOME:  snprintf(buf, buflen, "Home");      return;
        case KEY_END:   snprintf(buf, buflen, "End");       return;
        case KEY_PPAGE: snprintf(buf, buflen, "PgUp");      return;
        case KEY_NPAGE: snprintf(buf, buflen, "PgDn");      return;
        case KEY_IC:    snprintf(buf, buflen, "Ins");       return;
        case KEY_DC:    snprintf(buf, buflen, "Del");       return;
        case KEY_F(1):  snprintf(buf, buflen, "F1");        return;
        case KEY_F(2):  snprintf(buf, buflen, "F2");        return;
        case KEY_F(3):  snprintf(buf, buflen, "F3");        return;
        case KEY_F(4):  snprintf(buf, buflen, "F4");        return;
        case KEY_F(5):  snprintf(buf, buflen, "F5");        return;
        case KEY_F(6):  snprintf(buf, buflen, "F6");        return;
        case KEY_F(7):  snprintf(buf, buflen, "F7");        return;
        case KEY_F(8):  snprintf(buf, buflen, "F8");        return;
        case KEY_F(9):  snprintf(buf, buflen, "F9");        return;
        case KEY_F(10): snprintf(buf, buflen, "F10");       return;
        case KEY_F(11): snprintf(buf, buflen, "F11");       return;
        case KEY_F(12): snprintf(buf, buflen, "F12");       return;
    }

    if(keycode >= 32 && keycode < 127)
    {
        snprintf(buf, buflen, "%c", (char)keycode);
        return;
    }

    snprintf(buf, buflen, "0x%04x", keycode);
}

/* ── listbox / entry mapping ───────────────────────────────── */

static int
lb_index_to_entry(int lb_idx)
{
    int i;

    for(i = 0; i < NUM_HOTKEYS; i++)
    {
        if(entry_to_lb[i] == lb_idx) return i;
    }

    return -1;
}

/* ── model / vwm sync ──────────────────────────────────────── */

/* the dialog's working copy takes the values vwm is using */
static void
model_load_from_vwm(vwm_t *vwm)
{
    int i;

    for(i = 0; i < NUM_HOTKEYS; i++)
        model->values[i] = *vwm_hotkey_field(vwm, i);
}

/* vwm takes the dialog's working copy */
static void
model_apply_to_vwm(vwm_t *vwm)
{
    int i;

    for(i = 0; i < NUM_HOTKEYS; i++)
        *vwm_hotkey_field(vwm, i) = model->values[i];
}

static void
model_load_from_config(const char *path)
{
    cJSON       *root;
    cJSON       *hotkeys;
    const char  *value;
    int32_t     keystroke;
    int         i;

    root = vwm_config_load(path);
    if(root == NULL) return;

    hotkeys = cJSON_GetObjectItemCaseSensitive(root, "hotkeys");

    for(i = 0; i < NUM_HOTKEYS; i++)
    {
        value = vwm_json_str(hotkeys, vwm_hotkeys[i].name, NULL);
        if(value != NULL && sscanf(value, "%x", &keystroke) == 1)
            model->values[i] = keystroke;
    }

    cJSON_Delete(root);
}

/* ── listbox rebuild ───────────────────────────────────────── */

static void
rebuild_listbox(void)
{
    int     i;
    int     lb_idx = 0;
    int     last_cat = -1;
    char    display[ITEM_WIDTH + 32];
    char    keybuf[32];

    vk_listbox_reset(hotkey_listbox);

    for(i = 0; i < NUM_HOTKEYS; i++)
    {
        if(hotkey_ui[i].category != last_cat)
        {
            vk_listbox_add_separator(hotkey_listbox, VK_SEPARATOR_SINGLE);
            lb_idx++;
            last_cat = hotkey_ui[i].category;
        }

        hotkey_format(model->values[i], keybuf, sizeof(keybuf));

        snprintf(display, sizeof(display), "  %-20s  %s",
            hotkey_ui[i].label, keybuf);

        vk_listbox_add_item(hotkey_listbox, display, NULL, NULL);
        entry_to_lb[i] = lb_idx;
        lb_idx++;
    }

}

/* ── highlight updates ─────────────────────────────────────── */

static void
update_button_highlights(void)
{
    int focus_zones[] =
    {
        FOCUS_BTN_MODIFY, FOCUS_BTN_RESET,
        FOCUS_BTN_SAVE, FOCUS_BTN_LOAD, FOCUS_BTN_CLOSE
    };
    int i;

    for(i = 0; i < NUM_BUTTONS; i++)
    {
        vk_button_release(buttons[i]);

        if(model->focus_zone == focus_zones[i])
        {
            vk_widget_set_colors(VK_WIDGET(buttons[i]),
                COLOR_YELLOW, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(buttons[i]), A_BOLD);
        }
        else
        {
            vk_widget_set_colors(VK_WIDGET(buttons[i]),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(buttons[i]), A_BOLD);
        }

    }

    if(model->focus_zone == FOCUS_HOTKEY_LIST)
    {
        vk_frame_set_border_colors(listbox_frame, COLOR_YELLOW, COLOR_CYAN);
        vk_frame_set_border_attrs(listbox_frame, A_BOLD);
    }
    else
    {
        vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
        vk_frame_set_border_attrs(listbox_frame, 0);
    }

    vk_listbox_set_focused(hotkey_listbox,
        model->focus_zone == FOCUS_HOTKEY_LIST);

}

/* ── error popup ───────────────────────────────────────────── */

static void
error_popup_close(void)
{
    vwm_popup_close(&error_popup, refresh_dialog);
}

static int
error_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    return vwm_notice_popup_kmio(&error_popup, keystroke,
        refresh_dialog, 0);
}

static void
error_popup_show(const char *msg)
{
    if(error_popup != NULL) return;

    error_popup = vwm_error_popup_show(msg, 46, 10);
    vk_object_set_kmio(VK_OBJECT(error_popup), error_popup_kmio);
}

/* ── saved popup ───────────────────────────────────────────── */

static void
saved_popup_close(void)
{
    vwm_popup_close(&saved_popup, refresh_dialog);
}

static int
saved_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    return vwm_notice_popup_kmio(&saved_popup, keystroke,
        refresh_dialog, 0);
}

static void
saved_popup_show(void)
{
    if(saved_popup != NULL) return;

    saved_popup = vwm_saved_popup_show("Hotkeys saved.");
    vk_object_set_kmio(VK_OBJECT(saved_popup), saved_popup_kmio);
}

/* ── warning popup ─────────────────────────────────────────── */

static void
warning_popup_close(void)
{
    vwm_popup_close(&warning_popup, refresh_dialog);
}

static int
warning_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    return vwm_notice_popup_kmio(&warning_popup, keystroke,
        refresh_dialog, 0);
}

static void
warning_popup_show(void)
{
    if(warning_popup != NULL) return;

    warning_popup = vwm_warning_popup_show();
    vk_object_set_kmio(VK_OBJECT(warning_popup), warning_popup_kmio);
}

/* ── duplicate check ───────────────────────────────────────── */

static bool
check_duplicates(char *errbuf, int errlen)
{
    int i, j;

    for(i = 0; i < NUM_HOTKEYS; i++)
    {
        for(j = i + 1; j < NUM_HOTKEYS; j++)
        {
            if(model->values[i] == model->values[j])
            {
                snprintf(errbuf, errlen,
                    "Duplicate: %s and %s use the same key",
                    hotkey_ui[i].label, hotkey_ui[j].label);
                return true;
            }
        }
    }

    return false;
}

/* ── dirty check ───────────────────────────────────────────── */

/* does the working copy differ from what vwm is using? */
static bool
has_changes(void)
{
    vwm_t   *vwm = vwm_get_instance();
    int     i;

    for(i = 0; i < NUM_HOTKEYS; i++)
    {
        if(model->values[i] != *vwm_hotkey_field(vwm, i)) return true;
    }

    return false;
}

/* ── confirm popup ─────────────────────────────────────────── */

static void
confirm_popup_close(void)
{
    vwm_popup_close(&confirm_popup, refresh_dialog);
}

static int
confirm_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    /* Discard closes the dialog; Cancel only closes this popup */
    return vwm_two_button_kmio(&confirm_popup, &confirm_active_btn,
        keystroke, vwm_manage_hotkeys_close, refresh_dialog, 0);
}

static void
confirm_popup_show(void)
{
    if(confirm_popup != NULL) return;

    confirm_popup = vwm_confirm_popup_show("Discard changes and close?");
    vk_object_set_kmio(VK_OBJECT(confirm_popup), confirm_popup_kmio);
    confirm_active_btn = 0;
}

/* ── load popup ────────────────────────────────────────────── */

/* Tab cycles, an empty selection closes, and OK ignores a directory */
#define HK_LOAD_FLAGS \
    (VWM_LOAD_TAB_CYCLES | VWM_LOAD_CLOSE_EMPTY | VWM_LOAD_REJECT_DIR)

/* a file was chosen: take those hotkeys and highlight the first row */
static void
hk_load_on_file(const char *path)
{
    strncpy(model->file_path, path, PATH_MAX - 1);
    model->file_path[PATH_MAX - 1] = '\0';

    model_load_from_config(model->file_path);
    model->selected = 0;

    rebuild_listbox();

    vk_listbox_set_curr(hotkey_listbox, entry_to_lb[0]);

    refresh_dialog();
}

static void
hk_load_popup_close(void)
{
    vwm_load_popup_close(&load_popup, &load_filedialog, refresh_dialog);
}

static void
hk_load_popup_ok(void)
{
    vwm_load_popup_ok(&load_popup, &load_filedialog, hk_load_on_file,
        refresh_dialog, HK_LOAD_FLAGS);
}

/*
    Highlight the focused Okay/Cancel button and toggle the file_list
    listbox between focused (BLACK/RED) and unfocused (BLACK/WHITE).
*/
static void
update_hk_load_focus(void)
{
    vwm_load_popup_paint_focus(load_filedialog, hk_load_focus);
}

static int
hk_load_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    /* Tab walks the file list and the buttons; Shift-Tab does not */
    return vwm_load_popup_kmio(&load_popup, &load_filedialog,
        &hk_load_focus, keystroke, hk_load_on_file, refresh_dialog,
        refresh_load_popup, HK_LOAD_FLAGS);
}

static void
refresh_load_popup(void)
{
    vwm_t *vwm;

    if(load_popup == NULL) return;

    vk_popup_redraw(load_popup);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

static void
hk_load_popup_open(void)
{
    if(load_popup != NULL) return;

    load_last_click_item = -1;
    memset(&load_last_click_time, 0, sizeof(load_last_click_time));

    hk_load_focus = HL_FILEDIALOG;

    load_popup = vwm_load_popup_show(" Load Config ", &load_filedialog,
        model->file_path);
    vk_object_set_kmio(VK_OBJECT(load_popup), hk_load_popup_kmio);

    update_hk_load_focus();
    refresh_load_popup();
}

/* ── actions ───────────────────────────────────────────────── */

static void
on_modify(void)
{
    if(model->selected < 0 || model->selected >= NUM_HOTKEYS) return;

    model->capture_mode = true;
    vwm_panel_set_status("Press any key to assign...");
}

static void
on_reset(void)
{
    if(model->selected < 0 || model->selected >= NUM_HOTKEYS) return;

    model->values[model->selected] =
        vwm_hotkeys[model->selected].default_value;

    rebuild_listbox();
    vk_listbox_set_curr(hotkey_listbox, entry_to_lb[model->selected]);
    refresh_dialog();
}

static void
on_save(void)
{
    vwm_t   *vwm;
    char    errbuf[128];

    if(check_duplicates(errbuf, sizeof(errbuf)))
    {
        error_popup_show(errbuf);
        return;
    }

    vwm = vwm_get_instance();
    model_apply_to_vwm(vwm);
    vwm_settings_save(vwm);

    saved_popup_show();
}

static void
on_load(void)
{
    hk_load_popup_open();
}

static void
on_close(void)
{
    if(has_changes())
    {
        confirm_popup_show();
        return;
    }

    vwm_manage_hotkeys_close();
}

/* ── keyboard handler ──────────────────────────────────────── */

/*
    Move the list's highlight one row up (dir < 0) or down, and note
    which hotkey it is now on.  The arrow keys and the mouse wheel both
    come here.
*/
static void
hotkey_list_step(int dir)
{
    int entry;

    if(dir < 0)
        vk_listbox_set_prev(hotkey_listbox);
    else
        vk_listbox_set_next(hotkey_listbox);

    /* a separator row maps to no hotkey: keep the old selection */
    entry = lb_index_to_entry(vk_listbox_get_curr(hotkey_listbox));
    if(entry >= 0) model->selected = entry;
}

static int
handle_hotkey_list_keys(int32_t keystroke)
{
    switch(keystroke)
    {
        case KEY_UP:
            hotkey_list_step(-1);
            return 0;

        case KEY_DOWN:
            hotkey_list_step(1);
            return 0;
    }

    return -1;
}

/* do what the button of focus zone `zone` does; the keyboard and the
   mouse both come here */
static void
button_activate(int zone)
{
    switch(zone)
    {
        case FOCUS_BTN_MODIFY:  on_modify();    break;
        case FOCUS_BTN_RESET:   on_reset();     break;
        case FOCUS_BTN_SAVE:    on_save();      break;
        case FOCUS_BTN_LOAD:    on_load();      break;
        case FOCUS_BTN_CLOSE:   on_close();     break;
    }
}

static int
handle_button_keys(int32_t keystroke)
{
    if(keystroke != KEY_CRLF && keystroke != ' ')
        return -1;

    button_activate(model->focus_zone);

    return 0;
}

static int
manage_hotkeys_kmio(vk_object_t *object, int32_t keystroke)
{
    int retval = -1;

    (void)object;

    if(confirm_popup != NULL)
        return confirm_popup_kmio(NULL, keystroke);

    if(error_popup != NULL)
        return error_popup_kmio(NULL, keystroke);

    if(saved_popup != NULL)
        return saved_popup_kmio(NULL, keystroke);

    if(warning_popup != NULL)
        return warning_popup_kmio(NULL, keystroke);

    if(load_popup != NULL)
        return hk_load_popup_kmio(NULL, keystroke);

    if(model->capture_mode)
    {
        if(keystroke == KEY_RESIZE || keystroke == KEY_MOUSE)
            return 0;

        model->values[model->selected] = keystroke;
        model->capture_mode = false;

        rebuild_listbox();
        vk_listbox_set_curr(hotkey_listbox,
            entry_to_lb[model->selected]);

        vwm_panel_set_status(MANAGE_HOTKEYS_HELP);
        refresh_dialog();
        return 0;
    }

    if(keystroke == 27)
    {
        on_close();
        return 0;
    }

    if(keystroke == '\t')
    {
        model->focus_zone++;
        if(model->focus_zone >= FOCUS_MAX)
            model->focus_zone = FOCUS_HOTKEY_LIST;

        refresh_dialog();
        return 0;
    }

    if(keystroke == KEY_BTAB)
    {
        model->focus_zone--;
        if(model->focus_zone < 0)
            model->focus_zone = FOCUS_MAX - 1;

        refresh_dialog();
        return 0;
    }

    switch(model->focus_zone)
    {
        case FOCUS_HOTKEY_LIST:
            retval = handle_hotkey_list_keys(keystroke);
            break;

        case FOCUS_BTN_MODIFY:
        case FOCUS_BTN_RESET:
        case FOCUS_BTN_SAVE:
        case FOCUS_BTN_LOAD:
        case FOCUS_BTN_CLOSE:
            retval = handle_button_keys(keystroke);
            break;
    }

    if(retval == 0)
        refresh_dialog();

    return retval == 0 ? 0 : -1;
}

/* ── build / refresh ───────────────────────────────────────── */

static void
refresh_dialog(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    update_button_highlights();
    vk_window_redraw(dialog_window);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

static void
build_dialog(void)
{
    vwm_t           *vwm;
    vk_filler_t     *button_spacer;
    int             scr_width, scr_height;
    int             pos_x, pos_y;
    int             lb_height;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    dialog_window = vk_window_create(DIALOG_WIDTH, DIALOG_HEIGHT);
    vk_window_set_title(dialog_window, " Manage Hotkeys ");
    vk_window_set_border_style(dialog_window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(dialog_window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(dialog_window, A_BOLD);

    vk_widget_set_noresize(VK_WIDGET(dialog_window));

    main_vbox = vk_box_create(INTERIOR_WIDTH, INTERIOR_HEIGHT,
        VK_BOX_VERTICAL, 2);
    vk_box_set_homogeneous(main_vbox, false);
    vk_widget_set_colors(VK_WIDGET(main_vbox), COLOR_BLACK, COLOR_CYAN);

    lb_height = INTERIOR_HEIGHT - 3 - 2;

    hotkey_listbox = vk_listbox_create(INTERIOR_WIDTH - 2, lb_height);
    vk_listbox_set_wrap(hotkey_listbox, FALSE);
    vk_listbox_set_highlight(hotkey_listbox, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_unfocused(hotkey_listbox, COLOR_BLACK, COLOR_WHITE);
    vk_widget_set_colors(VK_WIDGET(hotkey_listbox),
        COLOR_BLACK, COLOR_CYAN);

    listbox_frame = vk_frame_create(INTERIOR_WIDTH, lb_height + 2);
    vk_frame_set_border_style(listbox_frame,
        VK_BORDER_SINGLE | VK_RELIEF_SUNKEN);
    vk_frame_set_border_colors(listbox_frame, COLOR_YELLOW, COLOR_CYAN);
    vk_frame_set_border_attrs(listbox_frame, A_BOLD);
    vk_frame_set_child(listbox_frame, VK_WIDGET(hotkey_listbox), VK_INHERIT_NONE);
    vk_widget_set_expand(VK_WIDGET(listbox_frame));

    listbox_scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
    vk_scroller_set_border_style(listbox_scroller, VK_BORDER_SINGLE);
    vk_scroller_set_border_colors(listbox_scroller,
        COLOR_BLACK, COLOR_CYAN);
    vk_scroller_set_scroll_source(listbox_scroller,
        VK_WIDGET(hotkey_listbox));
    vk_scroller_set_scroll_info(listbox_scroller,
        vwm_listbox_scroll_info);
    vk_widget_attach_scroller(VK_WIDGET(hotkey_listbox),
        listbox_scroller);

    button_hbox = vk_box_create(INTERIOR_WIDTH, 3,
        VK_BOX_HORIZONTAL, 6);
    vk_box_set_homogeneous(button_hbox, false);

    buttons[BTN_MODIFY] = vk_button_create("Modify");
    buttons[BTN_RESET] = vk_button_create("Reset");
    buttons[BTN_SAVE] = vk_button_create("Save");
    buttons[BTN_LOAD] = vk_button_create("Load");
    buttons[BTN_CLOSE] = vk_button_create("Close");

    {
        int i;
        for(i = 0; i < NUM_BUTTONS; i++)
        {
            vk_button_set_border_style(buttons[i], VK_BORDER_SINGLE);
            vk_widget_set_colors(VK_WIDGET(buttons[i]),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(buttons[i]), A_BOLD);
            vk_button_set_pressed_colors(buttons[i],
                COLOR_WHITE, COLOR_BLUE);
        }
    }

    button_spacer = vk_filler_create();
    vk_widget_set_expand(VK_WIDGET(button_spacer));

    vk_box_set_widget(button_hbox, 0, VK_WIDGET(buttons[BTN_MODIFY]), VK_INHERIT_NONE);
    vk_box_set_widget(button_hbox, 1, VK_WIDGET(buttons[BTN_RESET]), VK_INHERIT_NONE);
    vk_box_set_widget(button_hbox, 2, VK_WIDGET(button_spacer), VK_INHERIT_AUTO);
    vk_box_set_widget(button_hbox, 3, VK_WIDGET(buttons[BTN_SAVE]), VK_INHERIT_NONE);
    vk_box_set_widget(button_hbox, 4, VK_WIDGET(buttons[BTN_LOAD]), VK_INHERIT_NONE);
    vk_box_set_widget(button_hbox, 5, VK_WIDGET(buttons[BTN_CLOSE]), VK_INHERIT_NONE);

    vk_box_set_widget(main_vbox, 0, VK_WIDGET(listbox_frame), VK_INHERIT_NONE);
    vk_box_set_widget(main_vbox, 1, VK_WIDGET(button_hbox), VK_INHERIT_AUTO);

    vk_window_set_child(dialog_window, VK_WIDGET(main_vbox), VK_INHERIT_NONE);
    vk_object_set_kmio(VK_OBJECT(dialog_window), manage_hotkeys_kmio);

    rebuild_listbox();

    model->selected = 0;
    vk_listbox_set_curr(hotkey_listbox, entry_to_lb[0]);

    update_button_highlights();

    pos_x = (scr_width - DIALOG_WIDTH) / 2;
    pos_y = (scr_height - DIALOG_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vwm->manage_hotkeys_popup = dialog_window;

    refresh_dialog();
}

/* ── public API ────────────────────────────────────────────── */

int
vwm_manage_hotkeys_open(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;
    char    *rc_file;

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
                "Terminal too small for Manage Hotkeys");
            return -1;
        }
    }

    model = (hotkey_model_t *)calloc(1, sizeof(hotkey_model_t));
    model->focus_zone = FOCUS_HOTKEY_LIST;

    list_last_click_item = -1;
    memset(&list_last_click_time, 0, sizeof(list_last_click_time));

    rc_file = vwm_profile_rc_file_get(vwm);
    if(rc_file != NULL)
        strncpy(model->file_path, rc_file, PATH_MAX - 1);

    model_load_from_vwm(vwm);

    build_dialog();

    vwm_panel_set_status(MANAGE_HOTKEYS_HELP);

    return 0;
}

void
vwm_manage_hotkeys_close(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    if(confirm_popup != NULL)
        confirm_popup_close();

    if(load_popup != NULL)
        hk_load_popup_close();

    if(error_popup != NULL)
        error_popup_close();

    if(saved_popup != NULL)
        saved_popup_close();

    if(warning_popup != NULL)
        warning_popup_close();

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vk_window_destroy(dialog_window);
    dialog_window = NULL;
    vwm->manage_hotkeys_popup = NULL;

    main_vbox = NULL;
    button_hbox = NULL;
    listbox_frame = NULL;
    listbox_scroller = NULL;
    hotkey_listbox = NULL;
    memset(buttons, 0, sizeof(buttons));

    free(model);
    model = NULL;

    vwm_panel_status_idle();

    vk_screen_refresh(vwm->screen);
}

bool
vwm_manage_hotkeys_is_open(void)
{
    return (dialog_window != NULL);
}

/*
    Which of this dialog's widgets is under the screen cell (mx, my):
    one of its popups, the dialog itself, or NULL when the cell is
    outside all of them.  The popups are listed topmost first.  This is
    all the mouse classifier needs to know about the dialog's insides.
*/
vk_widget_t *
vwm_manage_hotkeys_hit_test(int mx, int my)
{
    vk_widget_t *layers[] =
    {
        VK_WIDGET(confirm_popup),
        VK_WIDGET(error_popup),
        VK_WIDGET(saved_popup),
        VK_WIDGET(warning_popup),
        VK_WIDGET(load_popup),
        VK_WIDGET(dialog_window),
    };

    return vwm_layers_hit(layers,
        (int)(sizeof(layers) / sizeof(layers[0])), mx, my);
}

void
vwm_manage_hotkeys_handle_resize(void)
{
    vwm_t   *vwm;
    int     scr_w, scr_h;
    int     pos_x, pos_y;

    if(dialog_window == NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    if(scr_w < DIALOG_WIDTH || scr_h < DIALOG_HEIGHT)
    {
        vwm_manage_hotkeys_close();
        return;
    }

    pos_x = (scr_w - DIALOG_WIDTH) / 2;
    pos_y = (scr_h - DIALOG_HEIGHT) / 2;

    vk_widget_recreate(VK_WIDGET(dialog_window));
    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    if(warning_popup != NULL)
        warning_popup_close();

    warning_popup_show();

    refresh_dialog();
}

int
vwm_manage_hotkeys_mouse(MEVENT *mouse_event)
{
    int     dlg_x, dlg_y;
    int     rx, ry;
    mmask_t bs;
    int     frame_h;

    if(dialog_window == NULL) return -1;
    if(mouse_event == NULL) return -1;

    bs = mouse_event->bstate;

    if(confirm_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            int cp_x, cp_y, cp_w, cp_h;
            int cx, cy;

            vk_widget_get_position(VK_WIDGET(confirm_popup),
                &cp_x, &cp_y);
            vk_widget_get_metrics(VK_WIDGET(confirm_popup),
                &cp_w, &cp_h);

            cx = mouse_event->x - cp_x - 1;
            cy = mouse_event->y - cp_y - 1;

            if(cy >= cp_h - 4 && cy < cp_h - 2
                && cx >= 0 && cx < cp_w - 2)
            {
                int mid = (cp_w - 2) / 2;
                if(cx < mid)
                {
                    confirm_popup_close();
                    vwm_manage_hotkeys_close();
                }
                else
                {
                    confirm_popup_close();
                }
            }
        }

        return 0;
    }

    if(load_popup != NULL)
    {
        int lp_x, lp_y, lp_w, lp_h;
        int lx, ly;
        int interior_w, interior_h;
        int btn_h = 3;
        int input_h = 3;

        vk_widget_get_position(VK_WIDGET(load_popup), &lp_x, &lp_y);
        vk_widget_get_metrics(VK_WIDGET(load_popup), &lp_w, &lp_h);

        lx = mouse_event->x - lp_x - 1;
        ly = mouse_event->y - lp_y - 1;

        interior_w = lp_w - 2;
        interior_h = lp_h - 2;

        if(lx < 0 || lx >= interior_w || ly < 0 || ly >= interior_h)
            return 0;

        if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON4_PRESSED
            | BUTTON5_PRESSED)))
            return 0;

        if(ly < input_h)
        {
            vk_object_push_keystroke(VK_OBJECT(load_filedialog), '/');
            refresh_load_popup();
            return 0;
        }

        if(ly >= interior_h - btn_h)
        {
            int mid = interior_w / 2;

            if(lx < mid && (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            {
                hk_load_popup_ok();
                return 0;
            }
            else if(lx >= mid
                && (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            {
                hk_load_popup_close();
                return 0;
            }

            return 0;
        }

        {
            vk_listbox_t *file_list;
            int list_y;

            file_list = vk_filedialog_get_file_list(load_filedialog);
            /* -1 extra for the sunken-relief frame's top border */
            list_y = ly - input_h - 1;

            /* the wheel walks the file list, one row a notch */
            if(bs & (BUTTON4_PRESSED | BUTTON5_PRESSED))
            {
                if(bs & BUTTON4_PRESSED)
                    vk_listbox_set_prev(file_list);
                else
                    vk_listbox_set_next(file_list);
                refresh_load_popup();
                return 0;
            }

            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                int scroll_pos = vk_listbox_get_scroll_pos(file_list);
                int clicked = scroll_pos + list_y;
                int count = vk_listbox_get_item_count(file_list);

                if(clicked >= 0 && clicked < count)
                {
                    struct timespec now;
                    long elapsed_ms;
                    bool is_dblclick = false;

                    clock_gettime(CLOCK_MONOTONIC, &now);

                    if(clicked == load_last_click_item)
                    {
                        elapsed_ms =
                            (now.tv_sec - load_last_click_time.tv_sec)
                                * 1000
                            + (now.tv_nsec - load_last_click_time.tv_nsec)
                                / 1000000;

                        if(elapsed_ms >= 0 && elapsed_ms < 400)
                            is_dblclick = true;
                    }

                    load_last_click_time = now;
                    load_last_click_item = clicked;

                    vk_listbox_set_curr(file_list, clicked);
                    refresh_load_popup();

                    if(is_dblclick)
                    {
                        /* push Enter so the filedialog navigates into a
                           directory or accepts a regular file */
                        vk_object_push_keystroke(
                            VK_OBJECT(load_filedialog), KEY_CRLF);

                        const char *sel =
                            vk_filedialog_get_selected(load_filedialog);
                        if(sel != NULL && sel[0] != '\0')
                        {
                            int len = strlen(sel);
                            if(sel[len - 1] != '/'
                                && strcmp(sel, "..") != 0)
                            {
                                hk_load_popup_ok();
                                return 0;
                            }
                        }

                        refresh_load_popup();
                    }
                }
            }
        }

        return 0;
    }

    if(error_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            error_popup_close();

        return 0;
    }

    if(saved_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            saved_popup_close();

        return 0;
    }

    if(warning_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            warning_popup_close();

        return 0;
    }

    vk_widget_get_position(VK_WIDGET(dialog_window), &dlg_x, &dlg_y);

    rx = mouse_event->x - dlg_x - 1;
    ry = mouse_event->y - dlg_y - 1;

    if(rx < 0 || rx >= INTERIOR_WIDTH) return 0;
    if(ry < 0 || ry >= INTERIOR_HEIGHT) return 0;

    frame_h = INTERIOR_HEIGHT - 3;

    /* the wheel walks the list, one row a notch */
    if(bs & (BUTTON4_PRESSED | BUTTON5_PRESSED))
    {
        hotkey_list_step((bs & BUTTON4_PRESSED) ? -1 : 1);

        model->focus_zone = FOCUS_HOTKEY_LIST;
        refresh_dialog();
        return 0;
    }

    if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
        return 0;

    if(ry >= 1 && ry < frame_h - 1)
    {
        int scroll_pos = vk_listbox_get_scroll_pos(hotkey_listbox);
        int clicked_lb = scroll_pos + (ry - 1);
        int entry;
        struct timespec now;
        long elapsed_ms;
        bool is_dblclick = false;

        entry = lb_index_to_entry(clicked_lb);
        if(entry < 0) return 0;

        model->focus_zone = FOCUS_HOTKEY_LIST;

        clock_gettime(CLOCK_MONOTONIC, &now);

        if(entry == list_last_click_item)
        {
            elapsed_ms =
                (now.tv_sec - list_last_click_time.tv_sec) * 1000
                + (now.tv_nsec - list_last_click_time.tv_nsec)
                    / 1000000;

            if(elapsed_ms >= 0 && elapsed_ms < 400)
                is_dblclick = true;
        }

        list_last_click_time = now;
        list_last_click_item = entry;

        vk_listbox_set_curr(hotkey_listbox, clicked_lb);
        model->selected = entry;

        refresh_dialog();

        if(is_dblclick)
            on_modify();

        return 0;
    }

    if(ry >= frame_h && ry < INTERIOR_HEIGHT)
    {
        int zone = -1;
        int hit;

        /* ask the buttons where they are.  The column table this
           replaced had the Save / Load / Close group one column to the
           left of the buttons, so a click on Load's right edge closed
           the dialog.  buttons[] is in the order of the FOCUS_BTN_*
           values. */
        hit = vwm_button_bar_hit(buttons, NUM_BUTTONS, rx);
        if(hit >= 0) zone = FOCUS_BTN_MODIFY + hit;

        if(zone >= 0)
        {
            model->focus_zone = zone;
            refresh_dialog();

            button_activate(zone);
        }

        return 0;
    }

    return 0;
}
