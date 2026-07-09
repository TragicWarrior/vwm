#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include <ncursesw/curses.h>

#include <vdk.h>

#include "cJSON.h"
#include "config.h"
#include "vwm.h"
#include "modules.h"
#include "programs.h"
#include "profile.h"
#include "private.h"
#include "strings.h"
#include "panel.h"
#include "winman.h"
#include "manage_apps.h"
#include "manage_ui_common.h"

#define MANAGE_APPS_HELP \
"[Tab] cycle controls  [Up/Dn] navigate list  " \
"[Enter/Space] activate  [Esc] cancel"

#define DIALOG_WIDTH        68
#define DIALOG_HEIGHT       24
#define INTERIOR_WIDTH      (DIALOG_WIDTH - 2)
#define INTERIOR_HEIGHT     (DIALOG_HEIGHT - 2)

/* the VTerm Mode row is split into two columns: the mode dropdown on the
   left and the scrollback spinbutton on the right (flush to the right
   edge, an expanding gap between them).  Visibility + Start directory
   share the same two-column layout one row down. */
#define TERM_COL_WIDTH          36
#define SCROLLBACK_COL_WIDTH    26
#define VIS_COL_WIDTH           32
#define START_DIR_COL_WIDTH     30

/* scrollback stepping: 0 means "vterm default".  The first step up off 0
   jumps to SCROLLBACK_FLOOR and a step down off it returns to 0; in
   between, the spinbutton's own step (SCROLLBACK_STEP) applies.  vwm
   enforces the 0 <-> floor jump in the spinbutton's on_change so libviper
   stays generic. */
#define SCROLLBACK_STEP         5
#define SCROLLBACK_FLOOR        100

#define MAX_ENTRIES         128
#define MAX_PARAMS          256

enum
{
    FOCUS_APP_LIST = 0,
    FOCUS_CATEGORY,
    FOCUS_TERMINAL,
    FOCUS_SCROLLBACK,
    FOCUS_VISIBILITY,
    FOCUS_START_DIR,
    FOCUS_BTN_ADD,
    FOCUS_BTN_REMOVE,
    FOCUS_BTN_EDIT,
    FOCUS_BTN_SAVE,
    FOCUS_BTN_LOAD,
    FOCUS_BTN_CANCEL,
    FOCUS_MAX
};

#define NUM_BUTTONS         6
#define BTN_ADD             0
#define BTN_REMOVE          1
#define BTN_EDIT            2
#define BTN_SAVE            3
#define BTN_LOAD            4
#define BTN_CANCEL          5

typedef struct
{
    char    title[NAME_MAX];
    char    bin[PATH_MAX];
    char    params[MAX_PARAMS];
    char    requires[NAME_MAX];
    int     type;
    bool    hidden;
    int     scrollback;     /* vterm history lines; 0 = vterm default */
    bool    start_home;     /* true -> VTERM_FLAG_START_HOME at launch */
} manage_app_entry_t;

typedef struct
{
    manage_app_entry_t  entries[MAX_ENTRIES];
    int                 count;
    int                 selected;
    int                 focus_zone;
    bool                dirty;
    char                file_path[PATH_MAX];
} manage_app_model_t;

static const char *vterm_names[] =
{
    "vterm-vt100",
    "vterm-color",
    "vterm-xterm",
    "vterm-xterm256",
    "vterm-truecolor",
};

static const char *vterm_labels[] =
{
    "VT100",
    "Color",
    "XTerm",
    "HiColor",
    "Truecolor",
};

#define NUM_VTERMS          5

static const int category_types[] =
{
    VWM_MOD_TYPE_TOOL,
    VWM_MOD_TYPE_System,
    VWM_MOD_TYPE_ACCESSORY,
    VWM_MOD_TYPE_GAME,
    VWM_MOD_TYPE_OFFICE,
    VWM_MOD_TYPE_NETWORK,
    VWM_MOD_TYPE_DEVELOPMENT,
    VWM_MOD_TYPE_MESSAGING,
    VWM_MOD_TYPE_WEB,
    VWM_MOD_TYPE_MULTIMEDIA,
    VWM_MOD_TYPE_MISC,
};

#define NUM_CATEGORIES      11

static manage_app_model_t   *model = NULL;
static vk_window_t          *dialog_window = NULL;
static vk_frame_t           *listbox_frame = NULL;
static vk_scroller_t        *listbox_scroller = NULL;
static vk_box_t             *main_vbox = NULL;
static vk_box_t             *button_hbox = NULL;
static vk_box_t             *term_label_row = NULL;
static vk_box_t             *term_widget_row = NULL;
static vk_box_t             *vis_label_row = NULL;
static vk_box_t             *vis_widget_row = NULL;
static vk_listbox_t         *app_listbox = NULL;
static vk_label_t           *cat_label_widget = NULL;
static vk_dropdown_t        *cat_dropdown = NULL;
static vk_label_t           *term_label_widget = NULL;
static vk_dropdown_t        *term_dropdown = NULL;
static vk_label_t           *scrollback_label_widget = NULL;
static vk_spinbutton_t      *scrollback_spin = NULL;
static int                   scrollback_prev = 0;  /* last value, for the
                                                      0 <-> floor jump */
static vk_label_t           *vis_label_widget = NULL;
static vk_dropdown_t        *vis_dropdown = NULL;
static vk_label_t           *start_dir_label_widget = NULL;
static vk_dropdown_t        *start_dir_dropdown = NULL;
static vk_button_t          *buttons[NUM_BUTTONS];

#define EDIT_WIDTH          50
#define EDIT_HEIGHT         18
#define EDIT_INTERIOR_W     (EDIT_WIDTH - 2)
#define EDIT_INTERIOR_H     (EDIT_HEIGHT - 2)

enum
{
    EDIT_FOCUS_TITLE = 0,
    EDIT_FOCUS_BINARY,
    EDIT_FOCUS_PARAMS,
    EDIT_FOCUS_BUTTONS,
    EDIT_FOCUS_MAX
};

#define EDIT_BTN_OK         0
#define EDIT_BTN_CANCEL     1

static vk_popup_t      *edit_popup = NULL;
static vk_box_t        *edit_client = NULL;
static vk_input_t      *edit_input_title = NULL;
static vk_input_t      *edit_input_binary = NULL;
static vk_input_t      *edit_input_params = NULL;
static int             edit_focus = EDIT_FOCUS_TITLE;
static int             edit_active_btn = EDIT_BTN_OK;
static bool            edit_is_add_mode = false;
static vk_dropdown_t   *active_dropdown = NULL;

static struct timespec      list_last_click_time;
static int                 list_last_click_item = -1;

static vk_popup_t          *load_popup = NULL;
static vk_filedialog_t     *load_filedialog = NULL;
static vk_popup_t          *warning_popup = NULL;
static vk_popup_t          *saved_popup = NULL;
static vk_popup_t          *confirm_popup = NULL;
static int                 confirm_active_btn = 0;
static struct timespec     load_last_click_time;
static int                 load_last_click_item = -1;

/* Load dialog Tab focus stops: file browser, then OK / Cancel */
enum
{
    LF_FILEDIALOG = 0,
    LF_OK,
    LF_CANCEL,
    LF_COUNT
};
static int                 load_focus = LF_FILEDIALOG;

static void listbox_rebuild(void);
static void refresh_dialog(void);
static void refresh_edit_popup(void);
static void refresh_load_popup(void);
static void update_load_focus(void);
static int  manage_apps_kmio(vk_object_t *object, int32_t keystroke);
static void saved_popup_show(void);
static void confirm_popup_show(void);
static void confirm_popup_close(void);

static int
vterm_index_from_name(const char *name)
{
    int i;

    if(name == NULL || name[0] == '\0') return 0;

    for(i = 0; i < NUM_VTERMS; i++)
    {
        if(strcmp(vterm_names[i], name) == 0) return i;
    }

    return 0;
}

static int
category_index_from_type(int type)
{
    int i;

    for(i = 0; i < NUM_CATEGORIES; i++)
    {
        if(category_types[i] == type) return i;
    }

    return NUM_CATEGORIES - 1;
}

static void
model_load_from_config(const char *path)
{
    cJSON               *root;
    cJSON               *programs;
    cJSON               *entry;
    const char          *str;

    model->count = 0;

    root = vwm_config_load(path);
    if(root == NULL) return;

    programs = cJSON_GetObjectItemCaseSensitive(root, "programs");
    if(!cJSON_IsArray(programs))
    {
        cJSON_Delete(root);
        return;
    }

    cJSON_ArrayForEach(entry, programs)
    {
        manage_app_entry_t *e;

        if(model->count >= MAX_ENTRIES) break;
        if(!cJSON_IsObject(entry)) continue;

        e = &model->entries[model->count];
        memset(e, 0, sizeof(*e));

        str = vwm_json_str(entry, "title", NULL);
        if(str != NULL) strncpy(e->title, str, NAME_MAX - 1);

        str = vwm_json_str(entry, "bin", NULL);
        if(str != NULL) strncpy(e->bin, str, PATH_MAX - 1);

        str = vwm_json_str(entry, "params", NULL);
        if(str != NULL) strncpy(e->params, str, MAX_PARAMS - 1);

        str = vwm_json_str(entry, "requires", NULL);
        if(str != NULL) strncpy(e->requires, str, NAME_MAX - 1);

        str = vwm_json_str(entry, "type", NULL);
        if(str != NULL)
        {
            int val = vwm_module_type_value((char *)str);
            e->type = (val == -1) ? VWM_MOD_TYPE_MISC : val;
        }
        else
        {
            e->type = VWM_MOD_TYPE_MISC;
        }

        e->hidden = (vwm_json_bool(entry, "hidden", 0) != 0);
        e->scrollback = vwm_json_int(entry, "scrollback", 0);
        e->start_home = (vwm_json_bool(entry, "start_home", 0) != 0);

        model->count++;
    }

    cJSON_Delete(root);
}

static int
model_save_to_config(const char *path)
{
    cJSON   *root;
    cJSON   *programs;
    int     i;

    root = vwm_config_load(path);
    if(root == NULL) root = cJSON_CreateObject();

    cJSON_DeleteItemFromObjectCaseSensitive(root, "programs");
    programs = cJSON_AddArrayToObject(root, "programs");

    for(i = 0; i < model->count; i++)
    {
        manage_app_entry_t *e = &model->entries[i];
        cJSON *entry = cJSON_CreateObject();

        if(e->requires[0] != '\0')
            cJSON_AddStringToObject(entry, "requires", e->requires);

        cJSON_AddStringToObject(entry, "title", e->title);
        cJSON_AddStringToObject(entry, "bin", e->bin);

        {
            const char *type_str = vwm_module_type_string(e->type);
            if(type_str == NULL) type_str = "Misc";
            cJSON_AddStringToObject(entry, "type", type_str);
        }

        if(e->params[0] != '\0')
            cJSON_AddStringToObject(entry, "params", e->params);

        if(e->hidden)
            cJSON_AddBoolToObject(entry, "hidden", 1);

        if(e->scrollback > 0)
            cJSON_AddNumberToObject(entry, "scrollback", e->scrollback);

        if(e->start_home)
            cJSON_AddBoolToObject(entry, "start_home", 1);

        cJSON_AddItemToArray(programs, entry);
    }

    vwm_config_store(path, root);
    cJSON_Delete(root);

    return 0;
}

static void
populate_dropdowns_from_entry(int idx)
{
    manage_app_entry_t *e;

    if(idx < 0 || idx >= model->count) return;

    e = &model->entries[idx];

    vk_dropdown_set_curr(cat_dropdown, category_index_from_type(e->type));
    vk_dropdown_update(cat_dropdown);

    vk_dropdown_set_curr(term_dropdown, vterm_index_from_name(e->requires));
    vk_dropdown_update(term_dropdown);

    vk_dropdown_set_curr(vis_dropdown, e->hidden ? 1 : 0);
    vk_dropdown_update(vis_dropdown);

    vk_dropdown_set_curr(start_dir_dropdown, e->start_home ? 1 : 0);
    vk_dropdown_update(start_dir_dropdown);

    vk_spinbutton_set_value(scrollback_spin, (double)e->scrollback);
    scrollback_prev = e->scrollback;   /* set_value doesn't fire on_change */
    vk_spinbutton_update(scrollback_spin);
}

static void
commit_dropdowns_to_entry(int idx)
{
    manage_app_entry_t *e;
    int  cat_sel, term_sel, vis_sel;
    bool changed = false;

    if(idx < 0 || idx >= model->count) return;

    e = &model->entries[idx];

    /* Only flag the model dirty when a commit actually mutates a
       field.  Tab/Shift-Tab between focus zones routes through here
       every time, so without the guard a single Tab after Save (e.g.
       to reach the Cancel button) would re-dirty the model and make
       Close warn about losing changes that don't exist. */

    cat_sel = vk_dropdown_get_curr(cat_dropdown);
    if(cat_sel >= 0 && cat_sel < NUM_CATEGORIES
        && e->type != category_types[cat_sel])
    {
        e->type = category_types[cat_sel];
        changed = true;
    }

    term_sel = vk_dropdown_get_curr(term_dropdown);
    if(term_sel >= 0 && term_sel < NUM_VTERMS
        && strncmp(e->requires, vterm_names[term_sel], NAME_MAX - 1) != 0)
    {
        strncpy(e->requires, vterm_names[term_sel], NAME_MAX - 1);
        e->requires[NAME_MAX - 1] = '\0';
        changed = true;
    }

    vis_sel = vk_dropdown_get_curr(vis_dropdown);
    if(e->hidden != (vis_sel == 1))
    {
        e->hidden = (vis_sel == 1);
        changed = true;
    }

    {
        int start_sel = vk_dropdown_get_curr(start_dir_dropdown);
        bool want_home = (start_sel == 1);

        if(e->start_home != want_home)
        {
            e->start_home = want_home;
            changed = true;
        }
    }

    {
        /* get_value folds any in-progress manual edit into the number */
        int sb_val = (int)vk_spinbutton_get_value(scrollback_spin);
        if(sb_val != e->scrollback)
        {
            e->scrollback = sb_val;
            changed = true;
        }
    }

    if(changed) model->dirty = true;
}

static void
listbox_rebuild(void)
{
    int i;

    vk_listbox_reset(app_listbox);

    for(i = 0; i < model->count; i++)
    {
        manage_app_entry_t *e = &model->entries[i];

        char display[NAME_MAX + 16];
        snprintf(display, sizeof(display), "%.64s (%.64s)", e->title, e->bin);

        vk_listbox_add_item(app_listbox, display, NULL, NULL);
    }

    vk_listbox_update(app_listbox);
}

static void update_dropdown_highlights(void);

static void
update_button_highlights(void)
{
    int focus_zones[] =
    {
        FOCUS_BTN_ADD, FOCUS_BTN_REMOVE, FOCUS_BTN_EDIT,
        FOCUS_BTN_SAVE, FOCUS_BTN_LOAD, FOCUS_BTN_CANCEL
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

        vk_button_update(buttons[i]);
    }

    update_dropdown_highlights();
}

static void
update_dropdown_highlights(void)
{
    vk_dropdown_t *dropdowns[] =
    {
        cat_dropdown, term_dropdown, vis_dropdown, start_dir_dropdown
    };
    int zones[] =
    {
        FOCUS_CATEGORY, FOCUS_TERMINAL, FOCUS_VISIBILITY, FOCUS_START_DIR
    };
    int i;

    if(model->focus_zone == FOCUS_APP_LIST)
    {
        vk_frame_set_border_colors(listbox_frame, COLOR_YELLOW, COLOR_CYAN);
        vk_frame_set_border_attrs(listbox_frame, A_BOLD);
    }
    else
    {
        vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
        vk_frame_set_border_attrs(listbox_frame, 0);
    }

    /* toggle listbox highlight (red focused / gray blurred) */
    vk_listbox_set_focused(app_listbox,
        model->focus_zone == FOCUS_APP_LIST);
    vk_listbox_update(app_listbox);

    vk_frame_update(listbox_frame);

    for(i = 0; i < 4; i++)
    {
        if(model->focus_zone == zones[i])
        {
            vk_widget_set_colors(VK_WIDGET(dropdowns[i]),
                COLOR_YELLOW, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(dropdowns[i]), A_BOLD);
        }
        else
        {
            vk_widget_set_colors(VK_WIDGET(dropdowns[i]),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(dropdowns[i]), A_BOLD);
        }

        vk_dropdown_update(dropdowns[i]);
    }

    /* scrollback spinbutton -- yellow when focused, like the dropdowns */
    if(model->focus_zone == FOCUS_SCROLLBACK)
        vk_widget_set_colors(VK_WIDGET(scrollback_spin),
            COLOR_YELLOW, COLOR_CYAN);
    else
        vk_widget_set_colors(VK_WIDGET(scrollback_spin),
            COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(scrollback_spin), A_BOLD);
    vk_spinbutton_update(scrollback_spin);
}

/* ── dropdown popup helpers ────────────────────────────────── */

/*
    Map a dialog dropdown to its interior (client) origin.  Direct children
    of main_vbox store y relative to the vbox (which fills the client), so
    get_position is fine.  Nested two-column controls only know their offset
    inside the hbox row -- resolve those from the same layout math the mouse
    hit-test uses so the popup anchors next to the control.
*/
static void
dropdown_interior_origin(vk_dropdown_t *dropdown, int *ix, int *iy)
{
    int frame_h = INTERIOR_HEIGHT - 15;
    int cat_dd  = frame_h + 1;
    int term_dd = cat_dd + 4;
    int vis_dd  = term_dd + 4;

    if(dropdown == term_dropdown)
    {
        *ix = 0;
        *iy = term_dd;
        return;
    }

    if(dropdown == vis_dropdown)
    {
        *ix = 0;
        *iy = vis_dd;
        return;
    }

    if(dropdown == start_dir_dropdown)
    {
        *ix = INTERIOR_WIDTH - START_DIR_COL_WIDTH;
        *iy = vis_dd;
        return;
    }

    /* category (and any future direct vbox child) */
    vk_widget_get_position(VK_WIDGET(dropdown), ix, iy);
}

static void
dropdown_popup_attach(vk_dropdown_t *dropdown)
{
    vwm_t       *vwm;
    vk_widget_t *popup;
    int         dd_x, dd_y, dd_w, dd_h;
    int         pp_w, pp_h;
    int         dlg_x, dlg_y;
    int         popup_x, popup_y;
    int         scr_w, scr_h;

    popup = vk_dropdown_get_popup(dropdown);
    if(popup == NULL) return;

    vk_window_set_border_colors(VK_WINDOW(popup), COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(VK_WINDOW(popup), A_BOLD);

    {
        vk_widget_t *popup_child = vk_window_get_child(VK_WINDOW(popup));
        if(popup_child != NULL)
        {
            vk_widget_set_colors(popup_child, COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_attrs(popup_child, 0);
            vk_listbox_update(VK_LISTBOX(popup_child));
        }
    }

    vk_window_update(VK_WINDOW(popup));

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    vk_widget_get_position(VK_WIDGET(dialog_window), &dlg_x, &dlg_y);
    dropdown_interior_origin(dropdown, &dd_x, &dd_y);
    vk_widget_get_metrics(VK_WIDGET(dropdown), &dd_w, &dd_h);
    vk_widget_get_metrics(popup, &pp_w, &pp_h);

    /* dialog border is 1 cell; client origin is (dlg_x+1, dlg_y+1).
       Place the popup so its top edge sits on the control's bottom
       border (dd_h - 1) -- a one-row overlap removes the visual gap
       and reads as one cohesive widget.  The flip-above path shares
       the control's top border the same way. */
    popup_x = dlg_x + 1 + dd_x;
    popup_y = dlg_y + 1 + dd_y + dd_h - 1;

    if(popup_y + pp_h > scr_h)
        popup_y = dlg_y + 1 + dd_y - pp_h + 1;

    if(popup_y < 0) popup_y = 0;
    if(popup_x + pp_w > scr_w) popup_x = scr_w - pp_w;
    if(popup_x < 0) popup_x = 0;

    vk_widget_move(popup, popup_x, popup_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen), popup);

    active_dropdown = dropdown;
}

static void
dropdown_popup_detach(void)
{
    vwm_t       *vwm;
    vk_widget_t *popup;

    if(active_dropdown == NULL) return;

    popup = vk_dropdown_get_popup(active_dropdown);
    if(popup != NULL)
    {
        vwm = vwm_get_instance();
        vk_screen_detach_widget(vwm->screen,
            vk_screen_get_active_surface(vwm->screen), popup);
    }

    active_dropdown = NULL;
}

/* ── edit popup ─────────────────────────────────────────────── */

static void
edit_popup_close(void)
{
    vwm_t *vwm;

    if(edit_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(edit_popup));

    vk_popup_destroy(edit_popup);
    edit_popup = NULL;
    edit_client = NULL;
    edit_input_title = NULL;
    edit_input_binary = NULL;
    edit_input_params = NULL;
    edit_is_add_mode = false;

    refresh_dialog();
}

static void
edit_popup_ok(void)
{
    manage_app_entry_t  *e;
    const char          *text;

    if(edit_is_add_mode)
    {
        if(model->count >= MAX_ENTRIES)
        {
            edit_popup_close();
            return;
        }

        e = &model->entries[model->count];
        memset(e, 0, sizeof(*e));
        strncpy(e->requires, "vterm-color", NAME_MAX - 1);
        e->type = VWM_MOD_TYPE_TOOL;
        e->hidden = false;
    }
    else
    {
        if(model->selected < 0 || model->selected >= model->count)
        {
            edit_popup_close();
            return;
        }

        e = &model->entries[model->selected];
    }

    text = vk_input_get_text(edit_input_title);
    if(text != NULL)
    {
        memset(e->title, 0, NAME_MAX);
        strncpy(e->title, text, NAME_MAX - 1);
    }

    text = vk_input_get_text(edit_input_binary);
    if(text != NULL)
    {
        memset(e->bin, 0, PATH_MAX);
        strncpy(e->bin, text, PATH_MAX - 1);
    }

    text = vk_input_get_text(edit_input_params);
    if(text != NULL)
    {
        memset(e->params, 0, MAX_PARAMS);
        strncpy(e->params, text, MAX_PARAMS - 1);
    }

    if(edit_is_add_mode)
    {
        model->count++;
        model->selected = model->count - 1;
    }

    model->dirty = true;
    edit_popup_close();
    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, model->selected);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(model->selected);
    }

    refresh_dialog();
}

static void
update_edit_button_highlights(void)
{
    int i;
    int count;

    if(edit_popup == NULL) return;

    count = vk_popup_get_button_count(edit_popup);

    for(i = 0; i < count; i++)
    {
        vk_button_t *btn = vk_popup_get_button(edit_popup, i);

        vk_button_release(btn);

        if(edit_focus == EDIT_FOCUS_BUTTONS && i == edit_active_btn)
        {
            vk_widget_set_colors(VK_WIDGET(btn),
                COLOR_YELLOW, COLOR_BLUE);
            vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        }
        else
        {
            vk_widget_set_colors(VK_WIDGET(btn),
                COLOR_WHITE, COLOR_BLUE);
            vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
        }

        vk_button_update(btn);
    }
}

static void
update_edit_input_highlights(void)
{
    vk_input_t *inputs[3];
    int i;

    inputs[0] = edit_input_title;
    inputs[1] = edit_input_binary;
    inputs[2] = edit_input_params;

    for(i = 0; i < 3; i++)
    {
        if(edit_focus == EDIT_FOCUS_TITLE + i)
        {
            vk_widget_set_colors(VK_WIDGET(inputs[i]),
                COLOR_CYAN, COLOR_BLUE);
            vk_input_show_cursor(inputs[i], true);
        }
        else
        {
            vk_widget_set_colors(VK_WIDGET(inputs[i]),
                COLOR_WHITE, COLOR_BLUE);
            vk_input_show_cursor(inputs[i], false);
        }

        vk_input_update(inputs[i]);
    }
}

static void
refresh_edit_popup(void)
{
    vwm_t *vwm;

    if(edit_popup == NULL) return;

    if(edit_client != NULL)
        vk_box_update(edit_client);

    vk_popup_update(edit_popup);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

static int
handle_edit_input_keys(vk_input_t *input, int32_t keystroke)
{
    switch(keystroke)
    {
        case KEY_LEFT:
            vk_input_move_cursor(input, -1);
            vk_input_update(input);
            return 0;

        case KEY_RIGHT:
            vk_input_move_cursor(input, 1);
            vk_input_update(input);
            return 0;

        case KEY_HOME:
            vk_input_home(input);
            vk_input_update(input);
            return 0;

        case KEY_END:
            vk_input_end(input);
            vk_input_update(input);
            return 0;

        case KEY_BACKSPACE:
        case 127:
            vk_input_backspace(input);
            vk_input_update(input);
            return 0;

        case KEY_DC:
            vk_input_delete(input);
            vk_input_update(input);
            return 0;

        default:
            if(keystroke >= 32 && keystroke < 127)
            {
                vk_input_insert_char(input, keystroke);
                vk_input_update(input);
                return 0;
            }
            break;
    }

    return -1;
}

static int
edit_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        edit_popup_close();
        return 0;
    }

    if(keystroke == '\t')
    {
        if(edit_focus == EDIT_FOCUS_BUTTONS
            && edit_active_btn < EDIT_BTN_CANCEL)
        {
            edit_active_btn++;
        }
        else
        {
            edit_focus++;
            if(edit_focus >= EDIT_FOCUS_MAX)
                edit_focus = EDIT_FOCUS_TITLE;
            if(edit_focus == EDIT_FOCUS_BUTTONS)
                edit_active_btn = EDIT_BTN_OK;
        }

        update_edit_input_highlights();
        update_edit_button_highlights();
        refresh_edit_popup();
        return 0;
    }

    if(keystroke == KEY_BTAB)
    {
        if(edit_focus == EDIT_FOCUS_BUTTONS
            && edit_active_btn > EDIT_BTN_OK)
        {
            edit_active_btn--;
        }
        else
        {
            edit_focus--;
            if(edit_focus < 0)
                edit_focus = EDIT_FOCUS_MAX - 1;
            if(edit_focus == EDIT_FOCUS_BUTTONS)
                edit_active_btn = EDIT_BTN_CANCEL;
        }

        update_edit_input_highlights();
        update_edit_button_highlights();
        refresh_edit_popup();
        return 0;
    }

    if(keystroke == KEY_CRLF && edit_focus != EDIT_FOCUS_BUTTONS)
    {
        edit_focus++;
        if(edit_focus >= EDIT_FOCUS_MAX)
            edit_focus = EDIT_FOCUS_TITLE;

        update_edit_input_highlights();
        update_edit_button_highlights();
        refresh_edit_popup();
        return 0;
    }

    switch(edit_focus)
    {
        case EDIT_FOCUS_TITLE:
            if(handle_edit_input_keys(edit_input_title, keystroke) == 0)
            {
                refresh_edit_popup();
                return 0;
            }
            break;

        case EDIT_FOCUS_BINARY:
            if(handle_edit_input_keys(edit_input_binary, keystroke) == 0)
            {
                refresh_edit_popup();
                return 0;
            }
            break;

        case EDIT_FOCUS_PARAMS:
            if(handle_edit_input_keys(edit_input_params, keystroke) == 0)
            {
                refresh_edit_popup();
                return 0;
            }
            break;

        case EDIT_FOCUS_BUTTONS:
            switch(keystroke)
            {
                case KEY_LEFT:
                case KEY_RIGHT:
                    edit_active_btn = (edit_active_btn == 0) ? 1 : 0;
                    update_edit_button_highlights();
                    refresh_edit_popup();
                    return 0;

                case KEY_CRLF:
                case ' ':
                    if(edit_active_btn == EDIT_BTN_OK)
                        edit_popup_ok();
                    else
                        edit_popup_close();
                    return 0;
            }
            break;
    }

    return -1;
}

static void
edit_popup_open(void)
{
    vwm_t               *vwm;
    manage_app_entry_t  *e;
    vk_label_t          *lbl;
    int                 scr_width, scr_height;
    int                 pos_x, pos_y;

    if(edit_popup != NULL) return;

    if(edit_is_add_mode)
    {
        static manage_app_entry_t add_defaults;
        memset(&add_defaults, 0, sizeof(add_defaults));
        strncpy(add_defaults.title, "New App", NAME_MAX - 1);
        strncpy(add_defaults.bin, "/usr/bin/", PATH_MAX - 1);
        add_defaults.type = VWM_MOD_TYPE_TOOL;
        add_defaults.hidden = false;
        e = &add_defaults;
    }
    else
    {
        if(model->selected < 0 || model->selected >= model->count) return;
        e = &model->entries[model->selected];
    }
    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    edit_popup = vk_popup_create(EDIT_WIDTH, EDIT_HEIGHT,
        VK_BORDER_SINGLE, "Apply", "Cancel", NULL);
    vk_popup_set_title(edit_popup, edit_is_add_mode ? " Add App " : " Edit App ");
    vk_popup_set_border_colors(edit_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(edit_popup, A_BOLD);
    vk_popup_set_colors(edit_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_button_colors(edit_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_button_attrs(edit_popup, A_BOLD);

    edit_client = vk_box_create(EDIT_INTERIOR_W, EDIT_INTERIOR_H - 3,
        VK_BOX_VERTICAL, 6);
    vk_box_set_homogeneous(edit_client, false);
    vk_widget_set_colors(VK_WIDGET(edit_client), COLOR_WHITE, COLOR_BLUE);

    lbl = vk_label_create(EDIT_INTERIOR_W);
    vk_label_set_text(lbl, "  Title");
    vk_widget_set_colors(VK_WIDGET(lbl), COLOR_WHITE, COLOR_BLUE);
    vk_label_update(lbl);
    vk_box_set_widget(edit_client, 0, VK_WIDGET(lbl));

    edit_input_title = vk_input_create(EDIT_INTERIOR_W);
    vk_input_set_border_style(edit_input_title, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(edit_input_title), COLOR_BLACK, COLOR_BLUE);
    vk_input_set_text(edit_input_title, e->title);
    vk_input_update(edit_input_title);
    vk_box_set_widget(edit_client, 1, VK_WIDGET(edit_input_title));

    lbl = vk_label_create(EDIT_INTERIOR_W);
    vk_label_set_text(lbl, "  Binary");
    vk_widget_set_colors(VK_WIDGET(lbl), COLOR_WHITE, COLOR_BLUE);
    vk_label_update(lbl);
    vk_box_set_widget(edit_client, 2, VK_WIDGET(lbl));

    edit_input_binary = vk_input_create(EDIT_INTERIOR_W);
    vk_input_set_border_style(edit_input_binary, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(edit_input_binary), COLOR_BLACK, COLOR_BLUE);
    vk_input_set_text(edit_input_binary, e->bin);
    vk_input_update(edit_input_binary);
    vk_box_set_widget(edit_client, 3, VK_WIDGET(edit_input_binary));

    lbl = vk_label_create(EDIT_INTERIOR_W);
    vk_label_set_text(lbl, "  Params   (%fd = file picker at launch)");
    vk_widget_set_colors(VK_WIDGET(lbl), COLOR_WHITE, COLOR_BLUE);
    vk_label_update(lbl);
    vk_box_set_widget(edit_client, 4, VK_WIDGET(lbl));

    edit_input_params = vk_input_create(EDIT_INTERIOR_W);
    vk_input_set_border_style(edit_input_params, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(edit_input_params), COLOR_BLACK, COLOR_BLUE);
    vk_input_set_text(edit_input_params, e->params);
    vk_input_update(edit_input_params);
    vk_box_set_widget(edit_client, 5, VK_WIDGET(edit_input_params));

    vk_popup_set_client(edit_popup, VK_WIDGET(edit_client));
    vk_object_set_kmio(VK_OBJECT(edit_popup), edit_popup_kmio);

    edit_focus = EDIT_FOCUS_TITLE;
    edit_active_btn = EDIT_BTN_OK;
    update_edit_input_highlights();
    update_edit_button_highlights();

    pos_x = (scr_width - EDIT_WIDTH) / 2;
    pos_y = (scr_height - EDIT_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(edit_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(edit_popup));

    refresh_edit_popup();
}

/* ── load popup ─────────────────────────────────────────────── */

static void
load_popup_close(void)
{
    vwm_t *vwm;

    if(load_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(load_popup));

    vk_popup_destroy(load_popup);
    load_popup = NULL;
    load_filedialog = NULL;

    refresh_dialog();
}

static void
load_popup_ok(void)
{
    const char  *path;
    const char  *selected;
    char        fullpath[PATH_MAX];

    if(load_filedialog == NULL) return;

    path = vk_filedialog_get_path(load_filedialog);
    selected = vk_filedialog_get_selected(load_filedialog);

    if(path == NULL || selected == NULL)
    {
        load_popup_close();
        return;
    }

    if(selected[strlen(selected) - 1] == '/')
        return;

    if(strcmp(path, "/") == 0)
        snprintf(fullpath, sizeof(fullpath), "/%s", selected);
    else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, selected);

    strncpy(model->file_path, fullpath, PATH_MAX - 1);
    model->file_path[PATH_MAX - 1] = '\0';

    load_popup_close();

    model_load_from_config(model->file_path);
    model->selected = 0;
    model->dirty = false;

    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, 0);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(0);
    }

    refresh_dialog();
}

/* highlight the focused OK/Cancel button (the buttons live in the
   filedialog's button bar, reached via the public box getter) */
static void
update_load_focus(void)
{
    vwm_load_popup_paint_focus(load_filedialog, load_focus);
}

static int
load_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        load_popup_close();
        return 0;
    }

    if(keystroke == '\t')
    {
        load_focus = (load_focus + 1) % LF_COUNT;
        update_load_focus();
        refresh_load_popup();
        return 0;
    }

    if(load_focus == LF_OK)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            load_popup_ok();
        return 0;
    }

    if(load_focus == LF_CANCEL)
    {
        if(keystroke == KEY_CRLF || keystroke == ' ')
            load_popup_close();
        return 0;
    }

    /* LF_FILEDIALOG: drive the file browser */
    vk_object_push_keystroke(VK_OBJECT(load_filedialog), keystroke);
    vk_filedialog_update(load_filedialog);

    if(keystroke == KEY_CRLF)
    {
        const char *selected = vk_filedialog_get_selected(load_filedialog);
        if(selected != NULL && selected[0] != '\0')
        {
            int len = strlen(selected);
            if(selected[len - 1] != '/' && strcmp(selected, "..") != 0)
            {
                load_popup_ok();
                return 0;
            }
        }
    }

    refresh_load_popup();

    return 0;
}

static void
refresh_load_popup(void)
{
    vwm_t *vwm;

    if(load_popup == NULL) return;

    vk_filedialog_update(load_filedialog);
    vk_popup_update(load_popup);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

static void
load_popup_open(void)
{
    if(load_popup != NULL) return;

    load_last_click_item = -1;
    memset(&load_last_click_time, 0, sizeof(load_last_click_time));
    load_focus = LF_FILEDIALOG;

    load_popup = vwm_load_popup_show(" Load Config ", &load_filedialog,
        model->file_path);
    vk_object_set_kmio(VK_OBJECT(load_popup), load_popup_kmio);

    update_load_focus();
    refresh_load_popup();
}

/* ── main dialog callbacks ──────────────────────────────────── */

static void
on_add(void)
{
    if(model->count >= MAX_ENTRIES) return;

    edit_is_add_mode = true;
    edit_popup_open();
}

static void
on_remove(void)
{
    int i;

    if(model->count == 0) return;
    if(model->selected < 0 || model->selected >= model->count) return;

    for(i = model->selected; i < model->count - 1; i++)
        model->entries[i] = model->entries[i + 1];

    model->count--;
    model->dirty = true;

    if(model->selected >= model->count && model->count > 0)
        model->selected = model->count - 1;

    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, model->selected);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(model->selected);
    }

    refresh_dialog();
}

static void
on_edit(void)
{
    if(model->count == 0) return;
    if(model->selected < 0 || model->selected >= model->count) return;

    edit_popup_open();
}

static void
on_save(void)
{
    commit_dropdowns_to_entry(model->selected);
    model_save_to_config(model->file_path);
    model->dirty = false;

    vwm_programs_reload();

    saved_popup_show();
}

static void
on_load(void)
{
    load_popup_open();
}

static bool
has_changes(void)
{
    /* Flush a pending dropdown edit (category/terminal/visibility) into
       the model so it counts -- commit_dropdowns_to_entry only sets the
       dirty flag when a field actually changed.  on_save flushes the
       same way before saving; without this, Close skips the unsaved-
       changes prompt for a dropdown change that was never committed. */
    commit_dropdowns_to_entry(model->selected);

    return model->dirty;
}

static void
on_cancel(void)
{
    if(has_changes())
    {
        confirm_popup_show();
        return;
    }

    vwm_manage_apps_close();
}

/* ── warning popup ─────────────────────────────────────────── */

static void
warning_popup_close(void)
{
    vwm_t *vwm;

    if(warning_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(warning_popup));

    vk_popup_destroy(warning_popup);
    warning_popup = NULL;

    refresh_dialog();
}

static int
warning_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27 || keystroke == KEY_CRLF || keystroke == ' ')
    {
        warning_popup_close();
        return 0;
    }

    return 0;
}

static void
warning_popup_show(void)
{
    if(warning_popup != NULL) return;

    warning_popup = vwm_warning_popup_show();
    vk_object_set_kmio(VK_OBJECT(warning_popup), warning_popup_kmio);
}

/* ── saved popup ───────────────────────────────────────────── */

static void
saved_popup_close(void)
{
    vwm_t *vwm;

    if(saved_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(saved_popup));

    vk_popup_destroy(saved_popup);
    saved_popup = NULL;

    refresh_dialog();
}

static int
saved_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27 || keystroke == KEY_CRLF || keystroke == ' ')
    {
        saved_popup_close();
        return 0;
    }

    return 0;
}

static void
saved_popup_show(void)
{
    if(saved_popup != NULL) return;

    saved_popup = vwm_saved_popup_show("Apps saved.");
    vk_object_set_kmio(VK_OBJECT(saved_popup), saved_popup_kmio);
}

/* ── confirm-discard popup ─────────────────────────────────── */

static void
confirm_popup_close(void)
{
    vwm_t *vwm;

    if(confirm_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(confirm_popup));

    vk_popup_destroy(confirm_popup);
    confirm_popup = NULL;

    refresh_dialog();
}

static int
confirm_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        confirm_popup_close();
        return 0;
    }

    if(keystroke == '\t' || keystroke == KEY_RIGHT || keystroke == KEY_LEFT)
    {
        int count = vk_popup_get_button_count(confirm_popup);
        confirm_active_btn = (confirm_active_btn + 1) % count;

        for(int i = 0; i < count; i++)
        {
            vk_button_t *btn = vk_popup_get_button(confirm_popup, i);
            vk_button_release(btn);

            if(i == confirm_active_btn)
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_YELLOW, COLOR_WHITE);
                vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
            }
            else
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_BLACK, COLOR_WHITE);
                vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
            }

            vk_button_update(btn);
        }

        vk_popup_update(confirm_popup);

        vwm_t *vwm = vwm_get_instance();
        vk_screen_refresh(vwm->screen);
        return 0;
    }

    if(keystroke == KEY_CRLF || keystroke == ' ')
    {
        if(confirm_active_btn == 0)
        {
            confirm_popup_close();
            vwm_manage_apps_close();
        }
        else
        {
            confirm_popup_close();
        }

        return 0;
    }

    return 0;
}

static void
confirm_popup_show(void)
{
    if(confirm_popup != NULL) return;

    confirm_popup = vwm_confirm_popup_show();
    vk_object_set_kmio(VK_OBJECT(confirm_popup), confirm_popup_kmio);
    confirm_active_btn = 0;
}

/* ── keyboard handlers ──────────────────────────────────────── */

static int
handle_app_list_keys(int32_t keystroke)
{
    int new_sel;

    switch(keystroke)
    {
        case KEY_UP:
            vk_listbox_set_prev(app_listbox);
            vk_listbox_update(app_listbox);

            new_sel = vk_listbox_get_curr(app_listbox);
            if(new_sel != model->selected)
            {
                commit_dropdowns_to_entry(model->selected);
                model->selected = new_sel;
                populate_dropdowns_from_entry(model->selected);
            }
            return 0;

        case KEY_DOWN:
            vk_listbox_set_next(app_listbox);
            vk_listbox_update(app_listbox);

            new_sel = vk_listbox_get_curr(app_listbox);
            if(new_sel != model->selected)
            {
                commit_dropdowns_to_entry(model->selected);
                model->selected = new_sel;
                populate_dropdowns_from_entry(model->selected);
            }
            return 0;
    }

    return -1;
}

static int
handle_dropdown_keys(vk_dropdown_t *dropdown, int32_t keystroke)
{
    switch(keystroke)
    {
        case KEY_UP:
            vk_dropdown_set_prev(dropdown);
            vk_dropdown_update(dropdown);
            return 0;

        case KEY_DOWN:
            vk_dropdown_set_next(dropdown);
            vk_dropdown_update(dropdown);
            return 0;

        case ' ':
        case KEY_CRLF:
            vk_dropdown_set_expanded(dropdown, true);
            dropdown_popup_attach(dropdown);
            return 0;
    }

    return -1;
}

static int
handle_button_keys(int32_t keystroke)
{
    if(keystroke != KEY_CRLF && keystroke != ' ')
        return -1;

    switch(model->focus_zone)
    {
        case FOCUS_BTN_ADD:       on_add();       break;
        case FOCUS_BTN_REMOVE:    on_remove();    break;
        case FOCUS_BTN_EDIT:      on_edit();      break;
        case FOCUS_BTN_SAVE:      on_save();      break;
        case FOCUS_BTN_LOAD:      on_load();      break;
        case FOCUS_BTN_CANCEL:    on_cancel();    break;
    }

    return 0;
}

static int
manage_apps_kmio(vk_object_t *object, int32_t keystroke)
{
    int     retval = -1;

    (void)object;

    if(confirm_popup != NULL)
        return confirm_popup_kmio(NULL, keystroke);

    if(saved_popup != NULL)
        return saved_popup_kmio(NULL, keystroke);

    if(warning_popup != NULL)
        return warning_popup_kmio(NULL, keystroke);

    if(load_popup != NULL)
        return load_popup_kmio(NULL, keystroke);

    if(edit_popup != NULL)
        return edit_popup_kmio(NULL, keystroke);

    if(active_dropdown != NULL)
    {
        vk_dropdown_t *dd = active_dropdown;

        switch(keystroke)
        {
            case KEY_UP:
                vk_dropdown_popup_navigate(dd, -1);
                {
                    vwm_t *vwm = vwm_get_instance();
                    vk_screen_refresh(vwm->screen);
                }
                return 0;

            case KEY_DOWN:
                vk_dropdown_popup_navigate(dd, 1);
                {
                    vwm_t *vwm = vwm_get_instance();
                    vk_screen_refresh(vwm->screen);
                }
                return 0;

            case KEY_CRLF:
            case ' ':
                dropdown_popup_detach();
                vk_dropdown_popup_select(dd);
                refresh_dialog();
                return 0;

            case 27:
                dropdown_popup_detach();
                vk_dropdown_set_expanded(dd, false);
                refresh_dialog();
                return 0;

            case '\t':
            case KEY_BTAB:
                dropdown_popup_detach();
                vk_dropdown_set_expanded(dd, false);
                break;

            default:
                return 0;
        }
    }

    if(keystroke == 27)
    {
        on_cancel();
        return 0;
    }

    if(keystroke == '\t')
    {
        commit_dropdowns_to_entry(model->selected);
        model->focus_zone++;
        if(model->focus_zone >= FOCUS_MAX)
            model->focus_zone = FOCUS_APP_LIST;

        update_button_highlights();
        refresh_dialog();
        return 0;
    }

    if(keystroke == KEY_BTAB)
    {
        commit_dropdowns_to_entry(model->selected);
        model->focus_zone--;
        if(model->focus_zone < 0)
            model->focus_zone = FOCUS_MAX - 1;

        update_button_highlights();
        refresh_dialog();
        return 0;
    }

    switch(model->focus_zone)
    {
        case FOCUS_APP_LIST:
            retval = handle_app_list_keys(keystroke);
            break;

        case FOCUS_CATEGORY:
            retval = handle_dropdown_keys(cat_dropdown, keystroke);
            break;

        case FOCUS_TERMINAL:
            retval = handle_dropdown_keys(term_dropdown, keystroke);
            break;

        case FOCUS_SCROLLBACK:
            /* spinbutton's own kmio handles Up/Down and (since it is
               editable) typed digits; returns 0 when it consumed the key,
               else the keystroke -- normalized to 0/-1 by the tail */
            retval = vk_object_push_keystroke(VK_OBJECT(scrollback_spin),
                keystroke);
            break;

        case FOCUS_VISIBILITY:
            retval = handle_dropdown_keys(vis_dropdown, keystroke);
            break;

        case FOCUS_START_DIR:
            retval = handle_dropdown_keys(start_dir_dropdown, keystroke);
            break;

        case FOCUS_BTN_ADD:
        case FOCUS_BTN_REMOVE:
        case FOCUS_BTN_EDIT:
        case FOCUS_BTN_SAVE:
        case FOCUS_BTN_LOAD:
        case FOCUS_BTN_CANCEL:
            retval = handle_button_keys(keystroke);
            break;
    }

    if(retval == 0)
        refresh_dialog();

    return retval == 0 ? 0 : -1;
}

/* ── build / refresh ────────────────────────────────────────── */

static void
refresh_dialog(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    vk_label_update(cat_label_widget);
    vk_label_update(term_label_widget);
    vk_label_update(scrollback_label_widget);
    vk_label_update(vis_label_widget);
    vk_label_update(start_dir_label_widget);
    update_button_highlights();
    vk_listbox_update(app_listbox);
    vk_scroller_update(listbox_scroller);
    vk_frame_update(listbox_frame);
    vk_dropdown_update(cat_dropdown);
    vk_dropdown_update(term_dropdown);
    vk_dropdown_update(vis_dropdown);
    vk_dropdown_update(start_dir_dropdown);
    /* composite the two-column rows (children were just updated above)
       before the vbox composites them -- without this the hbox canvases
       stay blank and the row paints solid black */
    vk_box_update(term_label_row);
    vk_box_update(term_widget_row);
    vk_box_update(vis_label_row);
    vk_box_update(vis_widget_row);
    vk_box_update(button_hbox);
    vk_box_update(main_vbox);
    vk_window_update(dialog_window);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

/*
    Enforce the 0 <-> floor jump on the scrollback spinbutton.  The control
    steps by SCROLLBACK_STEP, so a step up off 0 lands on SCROLLBACK_STEP
    and a step down off the floor lands on SCROLLBACK_FLOOR - STEP; snap
    those two cases to the floor and to 0 respectively.  Manual entry of
    other values is left untouched (only the exact single-step results are
    intercepted).
*/
static int
scrollback_spin_on_change(vk_widget_t *widget, void *anything)
{
    vk_spinbutton_t *sb = VK_SPINBUTTON(widget);
    int             orig;
    int             v;

    (void)anything;

    orig = (int)vk_spinbutton_get_value(sb);
    v = orig;

    if(scrollback_prev == 0 && v == SCROLLBACK_STEP)
        v = SCROLLBACK_FLOOR;
    else if(scrollback_prev == SCROLLBACK_FLOOR
        && v == SCROLLBACK_FLOOR - SCROLLBACK_STEP)
        v = 0;

    if(v != orig)
        vk_spinbutton_set_value(sb, (double)v);

    scrollback_prev = v;

    return 0;
}

static void
build_dialog(void)
{
    vwm_t           *vwm;
    vk_filler_t     *button_spacer;
    vk_label_t      *cat_label;
    vk_label_t      *term_label;
    vk_label_t      *vis_label;
    int             scr_width, scr_height;
    int             pos_x, pos_y;
    int             lb_height;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    dialog_window = vk_window_create(DIALOG_WIDTH, DIALOG_HEIGHT);
    vk_window_set_title(dialog_window, " Manage Apps Menu ");
    vk_window_set_border_style(dialog_window, VK_BORDER_SINGLE);
    vk_window_set_border_colors(dialog_window, COLOR_WHITE, COLOR_CYAN);
    vk_window_set_border_attrs(dialog_window, A_BOLD);

    {
        uint32_t state = vk_widget_get_state(VK_WIDGET(dialog_window));
        vk_widget_set_state(VK_WIDGET(dialog_window),
            state | VK_STATE_NORESIZE);
    }

    main_vbox = vk_box_create(INTERIOR_WIDTH, INTERIOR_HEIGHT,
        VK_BOX_VERTICAL, 8);
    vk_box_set_homogeneous(main_vbox, false);
    vk_widget_set_colors(VK_WIDGET(main_vbox), COLOR_BLACK, COLOR_CYAN);

    /* non-expand rows: 1+3+1+3+1+3+3 = 15, expand gets 22-15-2 = 5 */
    lb_height = INTERIOR_HEIGHT - (1 + 3 + 1 + 3 + 1 + 3 + 3) - 2;

    app_listbox = vk_listbox_create(INTERIOR_WIDTH - 2, lb_height);
    vk_listbox_set_wrap(app_listbox, FALSE);
    vk_listbox_set_highlight(app_listbox, COLOR_BLACK, COLOR_RED);
    vk_listbox_set_unfocused(app_listbox, COLOR_BLACK, COLOR_WHITE);
    vk_widget_set_colors(VK_WIDGET(app_listbox), COLOR_BLACK, COLOR_CYAN);

    listbox_frame = vk_frame_create(INTERIOR_WIDTH, lb_height + 2);
    vk_frame_set_border_style(listbox_frame,
        VK_BORDER_SINGLE | VK_RELIEF_SUNKEN);
    vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
    vk_frame_set_child(listbox_frame, VK_WIDGET(app_listbox));
    vk_widget_set_expand(VK_WIDGET(listbox_frame));

    listbox_scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
    vk_scroller_set_border_style(listbox_scroller, VK_BORDER_SINGLE);
    vk_scroller_set_border_colors(listbox_scroller, COLOR_BLACK, COLOR_CYAN);
    vk_scroller_set_scroll_source(listbox_scroller,
        VK_WIDGET(app_listbox));
    vk_scroller_set_scroll_info(listbox_scroller,
        vwm_listbox_scroll_info);
    vk_widget_attach_scroller(VK_WIDGET(app_listbox),
        listbox_scroller);

    cat_label = vk_label_create(INTERIOR_WIDTH);
    vk_label_set_text(cat_label, "  Category");
    vk_label_set_justify(cat_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(cat_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(cat_label);
    cat_label_widget = cat_label;

    cat_dropdown = vk_dropdown_create(INTERIOR_WIDTH, 6);
    vk_dropdown_set_border_style(cat_dropdown, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(cat_dropdown), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(cat_dropdown), A_BOLD);
    vk_dropdown_set_highlight(cat_dropdown, COLOR_CYAN, COLOR_BLACK);

    {
        int i;
        for(i = 0; i < NUM_CATEGORIES; i++)
        {
            const char *type_str = vwm_module_type_string(category_types[i]);
            vk_dropdown_add_item(cat_dropdown,
                (char *)(type_str ? type_str : "?"), NULL, NULL);
        }
    }

    term_label = vk_label_create(TERM_COL_WIDTH);
    vk_label_set_text(term_label, "  VTerm Mode");
    vk_label_set_justify(term_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(term_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(term_label);
    term_label_widget = term_label;

    term_dropdown = vk_dropdown_create(TERM_COL_WIDTH, 5);
    vk_dropdown_set_border_style(term_dropdown, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(term_dropdown), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(term_dropdown), A_BOLD);
    vk_dropdown_set_highlight(term_dropdown, COLOR_CYAN, COLOR_BLACK);

    {
        int i;
        for(i = 0; i < NUM_VTERMS; i++)
            vk_dropdown_add_item(term_dropdown,
                (char *)vterm_labels[i], NULL, NULL);
    }

    /* right column of the VTerm Mode row: the scrollback spinbutton */
    scrollback_label_widget = vk_label_create(SCROLLBACK_COL_WIDTH);
    vk_label_set_text(scrollback_label_widget, "  Scrollback (lines)");
    vk_label_set_justify(scrollback_label_widget, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(scrollback_label_widget),
        COLOR_BLACK, COLOR_CYAN);
    vk_label_update(scrollback_label_widget);

    scrollback_spin = vk_spinbutton_create(SCROLLBACK_COL_WIDTH);
    vk_spinbutton_set_border_style(scrollback_spin, VK_BORDER_SINGLE);
    vk_spinbutton_set_field_relief(scrollback_spin, VK_RELIEF_SUNKEN);
    vk_spinbutton_set_button_relief(scrollback_spin, 0);   /* flat: a shared
        tee cell can't satisfy two relief directions, so the arrow box stays
        flat and the sunken field carries the 3D */
    vk_spinbutton_set_range(scrollback_spin, 0, 100000);
    vk_spinbutton_set_step(scrollback_spin, SCROLLBACK_STEP);
    vk_spinbutton_set_precision(scrollback_spin, 0);
    vk_spinbutton_set_editable(scrollback_spin, true);
    vk_spinbutton_set_on_change(scrollback_spin,
        scrollback_spin_on_change, NULL);
    vk_widget_set_colors(VK_WIDGET(scrollback_spin), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(scrollback_spin), A_BOLD);
    scrollback_prev = 0;   /* matches the freshly-created value (0) */

    vis_label = vk_label_create(VIS_COL_WIDTH);
    vk_label_set_text(vis_label, "  Visibility");
    vk_label_set_justify(vis_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(vis_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(vis_label);
    vis_label_widget = vis_label;

    vis_dropdown = vk_dropdown_create(VIS_COL_WIDTH, 2);
    vk_dropdown_set_border_style(vis_dropdown, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(vis_dropdown), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(vis_dropdown), A_BOLD);
    vk_dropdown_set_highlight(vis_dropdown, COLOR_CYAN, COLOR_BLACK);
    vk_dropdown_add_item(vis_dropdown, "Enabled", NULL, NULL);
    vk_dropdown_add_item(vis_dropdown, "Disabled", NULL, NULL);

    /* right column of the Visibility row: start directory */
    start_dir_label_widget = vk_label_create(START_DIR_COL_WIDTH);
    vk_label_set_text(start_dir_label_widget, "  Start directory");
    vk_label_set_justify(start_dir_label_widget, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(start_dir_label_widget),
        COLOR_BLACK, COLOR_CYAN);
    vk_label_update(start_dir_label_widget);

    start_dir_dropdown = vk_dropdown_create(START_DIR_COL_WIDTH, 2);
    vk_dropdown_set_border_style(start_dir_dropdown, VK_BORDER_SINGLE);
    vk_widget_set_colors(VK_WIDGET(start_dir_dropdown),
        COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(start_dir_dropdown), A_BOLD);
    vk_dropdown_set_highlight(start_dir_dropdown, COLOR_CYAN, COLOR_BLACK);
    vk_dropdown_add_item(start_dir_dropdown, "Working directory", NULL, NULL);
    vk_dropdown_add_item(start_dir_dropdown, "Home directory", NULL, NULL);

    /* buttons: Add Remove Edit | spacer | Save Load Cancel */
    button_hbox = vk_box_create(INTERIOR_WIDTH, 3,
        VK_BOX_HORIZONTAL, 7);
    vk_box_set_homogeneous(button_hbox, false);
    vk_widget_set_colors(VK_WIDGET(button_hbox), COLOR_BLACK, COLOR_CYAN);

    buttons[BTN_ADD] = vk_button_create("Add");
    buttons[BTN_REMOVE] = vk_button_create("Remove");
    buttons[BTN_EDIT] = vk_button_create("Edit");
    buttons[BTN_SAVE] = vk_button_create("Save");
    buttons[BTN_LOAD] = vk_button_create("Load");
    buttons[BTN_CANCEL] = vk_button_create("Close");

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
    vk_widget_set_colors(VK_WIDGET(button_spacer), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_expand(VK_WIDGET(button_spacer));

    vk_box_set_widget(button_hbox, 0, VK_WIDGET(buttons[BTN_ADD]));
    vk_box_set_widget(button_hbox, 1, VK_WIDGET(buttons[BTN_REMOVE]));
    vk_box_set_widget(button_hbox, 2, VK_WIDGET(buttons[BTN_EDIT]));
    vk_box_set_widget(button_hbox, 3, VK_WIDGET(button_spacer));
    vk_box_set_widget(button_hbox, 4, VK_WIDGET(buttons[BTN_SAVE]));
    vk_box_set_widget(button_hbox, 5, VK_WIDGET(buttons[BTN_LOAD]));
    vk_box_set_widget(button_hbox, 6, VK_WIDGET(buttons[BTN_CANCEL]));

    /*
        The VTerm Mode row is two columns: the mode dropdown (left) and
        the scrollback label/spinbutton (right), each in a horizontal box
        with an expanding filler between so the scrollback column sits
        flush to the right edge.  Slots 3 and 4 of the vbox hold these
        rows in place of the former full-width term label / dropdown.
    */
    {
        vk_filler_t *label_spacer;
        vk_filler_t *widget_spacer;

        term_label_row = vk_box_create(INTERIOR_WIDTH, 1,
            VK_BOX_HORIZONTAL, 3);
        vk_box_set_homogeneous(term_label_row, false);
        vk_widget_set_colors(VK_WIDGET(term_label_row),
            COLOR_BLACK, COLOR_CYAN);

        label_spacer = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(label_spacer), COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_expand(VK_WIDGET(label_spacer));

        vk_box_set_widget(term_label_row, 0, VK_WIDGET(term_label));
        vk_box_set_widget(term_label_row, 1, VK_WIDGET(label_spacer));
        vk_box_set_widget(term_label_row, 2,
            VK_WIDGET(scrollback_label_widget));

        term_widget_row = vk_box_create(INTERIOR_WIDTH, 3,
            VK_BOX_HORIZONTAL, 3);
        vk_box_set_homogeneous(term_widget_row, false);
        vk_widget_set_colors(VK_WIDGET(term_widget_row),
            COLOR_BLACK, COLOR_CYAN);

        widget_spacer = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(widget_spacer),
            COLOR_BLACK, COLOR_CYAN);
        vk_widget_set_expand(VK_WIDGET(widget_spacer));

        vk_box_set_widget(term_widget_row, 0, VK_WIDGET(term_dropdown));
        vk_box_set_widget(term_widget_row, 1, VK_WIDGET(widget_spacer));
        vk_box_set_widget(term_widget_row, 2, VK_WIDGET(scrollback_spin));

        /* Visibility + Start directory: same two-column layout */
        {
            vk_filler_t *vis_label_spacer;
            vk_filler_t *vis_widget_spacer;

            vis_label_row = vk_box_create(INTERIOR_WIDTH, 1,
                VK_BOX_HORIZONTAL, 3);
            vk_box_set_homogeneous(vis_label_row, false);
            vk_widget_set_colors(VK_WIDGET(vis_label_row),
                COLOR_BLACK, COLOR_CYAN);

            vis_label_spacer = vk_filler_create();
            vk_widget_set_colors(VK_WIDGET(vis_label_spacer),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_expand(VK_WIDGET(vis_label_spacer));

            vk_box_set_widget(vis_label_row, 0, VK_WIDGET(vis_label));
            vk_box_set_widget(vis_label_row, 1, VK_WIDGET(vis_label_spacer));
            vk_box_set_widget(vis_label_row, 2,
                VK_WIDGET(start_dir_label_widget));

            vis_widget_row = vk_box_create(INTERIOR_WIDTH, 3,
                VK_BOX_HORIZONTAL, 3);
            vk_box_set_homogeneous(vis_widget_row, false);
            vk_widget_set_colors(VK_WIDGET(vis_widget_row),
                COLOR_BLACK, COLOR_CYAN);

            vis_widget_spacer = vk_filler_create();
            vk_widget_set_colors(VK_WIDGET(vis_widget_spacer),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_expand(VK_WIDGET(vis_widget_spacer));

            vk_box_set_widget(vis_widget_row, 0, VK_WIDGET(vis_dropdown));
            vk_box_set_widget(vis_widget_row, 1, VK_WIDGET(vis_widget_spacer));
            vk_box_set_widget(vis_widget_row, 2,
                VK_WIDGET(start_dir_dropdown));
        }

        vk_box_set_widget(main_vbox, 0, VK_WIDGET(listbox_frame));
        vk_box_set_widget(main_vbox, 1, VK_WIDGET(cat_label));
        vk_box_set_widget(main_vbox, 2, VK_WIDGET(cat_dropdown));
        vk_box_set_widget(main_vbox, 3, VK_WIDGET(term_label_row));
        vk_box_set_widget(main_vbox, 4, VK_WIDGET(term_widget_row));
        vk_box_set_widget(main_vbox, 5, VK_WIDGET(vis_label_row));
        vk_box_set_widget(main_vbox, 6, VK_WIDGET(vis_widget_row));
        vk_box_set_widget(main_vbox, 7, VK_WIDGET(button_hbox));
    }

    vk_window_set_child(dialog_window, VK_WIDGET(main_vbox));
    vk_object_set_kmio(VK_OBJECT(dialog_window), manage_apps_kmio);

    listbox_rebuild();

    if(model->count > 0)
    {
        model->selected = 0;
        vk_listbox_set_curr(app_listbox, 0);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(0);
    }

    update_button_highlights();

    pos_x = (scr_width - DIALOG_WIDTH) / 2;
    pos_y = (scr_height - DIALOG_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vwm->manage_apps_popup = dialog_window;

    refresh_dialog();
}

/* ── public API ─────────────────────────────────────────────── */

int
vwm_manage_apps_open(vk_widget_t *widget, void *anything)
{
    vwm_t   *vwm;
    char    *rc_file;

    (void)widget;
    (void)anything;

    if(dialog_window != NULL) return -1;

    vwm = vwm_get_instance();

    model = (manage_app_model_t *)calloc(1, sizeof(manage_app_model_t));
    model->focus_zone = FOCUS_APP_LIST;

    list_last_click_item = -1;
    memset(&list_last_click_time, 0, sizeof(list_last_click_time));

    rc_file = vwm_profile_rc_file_get(vwm);
    if(rc_file != NULL)
        strncpy(model->file_path, rc_file, PATH_MAX - 1);

    model_load_from_config(model->file_path);

    build_dialog();

    vwm_panel_set_status(MANAGE_APPS_HELP);

    return 0;
}

void
vwm_manage_apps_close(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    if(confirm_popup != NULL)
        confirm_popup_close();

    if(saved_popup != NULL)
        saved_popup_close();

    if(warning_popup != NULL)
        warning_popup_close();

    if(load_popup != NULL)
        load_popup_close();

    if(active_dropdown != NULL)
        dropdown_popup_detach();

    if(edit_popup != NULL)
        edit_popup_close();

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vk_window_destroy(dialog_window);
    dialog_window = NULL;
    vwm->manage_apps_popup = NULL;

    main_vbox = NULL;
    button_hbox = NULL;
    term_label_row = NULL;
    term_widget_row = NULL;
    vis_label_row = NULL;
    vis_widget_row = NULL;
    listbox_frame = NULL;
    listbox_scroller = NULL;
    app_listbox = NULL;
    cat_label_widget = NULL;
    cat_dropdown = NULL;
    term_label_widget = NULL;
    term_dropdown = NULL;
    scrollback_label_widget = NULL;
    scrollback_spin = NULL;
    vis_label_widget = NULL;
    vis_dropdown = NULL;
    start_dir_label_widget = NULL;
    start_dir_dropdown = NULL;
    memset(buttons, 0, sizeof(buttons));

    free(model);
    model = NULL;

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
vwm_manage_apps_is_open(void)
{
    return (dialog_window != NULL);
}

vk_widget_t *
vwm_manage_apps_get_edit_popup(void)
{
    return VK_WIDGET(edit_popup);
}

vk_widget_t *
vwm_manage_apps_get_dropdown_popup(void)
{
    if(active_dropdown == NULL) return NULL;
    return vk_dropdown_get_popup(active_dropdown);
}

vk_widget_t *
vwm_manage_apps_get_load_popup(void)
{
    return VK_WIDGET(load_popup);
}

vk_widget_t *
vwm_manage_apps_get_warning_popup(void)
{
    return VK_WIDGET(warning_popup);
}

vk_widget_t *
vwm_manage_apps_get_saved_popup(void)
{
    return VK_WIDGET(saved_popup);
}

vk_widget_t *
vwm_manage_apps_get_confirm_popup(void)
{
    return VK_WIDGET(confirm_popup);
}

void
vwm_manage_apps_handle_resize(void)
{
    vwm_t   *vwm;
    int     scr_w, scr_h;
    int     pos_x, pos_y;

    if(dialog_window == NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    if(scr_w < DIALOG_WIDTH || scr_h < DIALOG_HEIGHT)
    {
        vwm_manage_apps_close();
        return;
    }

    pos_x = (scr_w - DIALOG_WIDTH) / 2;
    pos_y = (scr_h - DIALOG_HEIGHT) / 2;

    if(confirm_popup != NULL)
        confirm_popup_close();

    if(saved_popup != NULL)
        saved_popup_close();

    vk_widget_recreate(VK_WIDGET(dialog_window));
    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    if(warning_popup != NULL)
        warning_popup_close();

    warning_popup_show();

    refresh_dialog();
}

int
vwm_manage_apps_mouse(MEVENT *mouse_event)
{
    int     dlg_x, dlg_y;
    int     rx, ry;
    mmask_t bs;

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
                    vwm_manage_apps_close();
                }
                else
                {
                    confirm_popup_close();
                }
            }
        }

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
            load_focus = LF_FILEDIALOG;
            update_load_focus();
            vk_object_push_keystroke(VK_OBJECT(load_filedialog), '/');
            vk_filedialog_update(load_filedialog);
            refresh_load_popup();
            return 0;
        }

        if(ly >= interior_h - btn_h)
        {
            int mid = interior_w / 2;

            if(lx < mid && (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            {
                load_popup_ok();
                return 0;
            }
            else if(lx >= mid && (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            {
                load_popup_close();
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

            load_focus = LF_FILEDIALOG;
            update_load_focus();

            if(bs & BUTTON4_PRESSED)
            {
                vk_listbox_set_prev(file_list);
                vk_listbox_update(file_list);
                vk_filedialog_update(load_filedialog);
                refresh_load_popup();
                return 0;
            }

            if(bs & BUTTON5_PRESSED)
            {
                vk_listbox_set_next(file_list);
                vk_listbox_update(file_list);
                vk_filedialog_update(load_filedialog);
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
                    vk_listbox_update(file_list);
                    vk_filedialog_update(load_filedialog);
                    refresh_load_popup();

                    if(is_dblclick)
                    {
                        vk_object_push_keystroke(
                            VK_OBJECT(load_filedialog), KEY_CRLF);
                        vk_filedialog_update(load_filedialog);

                        const char *sel =
                            vk_filedialog_get_selected(load_filedialog);
                        if(sel != NULL && sel[0] != '\0')
                        {
                            int len = strlen(sel);
                            if(sel[len - 1] != '/'
                                && strcmp(sel, "..") != 0)
                            {
                                load_popup_ok();
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

    if(active_dropdown != NULL)
    {
        vk_dropdown_t *dd = active_dropdown;
        vk_widget_t *dd_pop = vk_dropdown_get_popup(dd);

        if(dd_pop != NULL)
        {
            int pp_x, pp_y, pp_w, pp_h;
            int px, py;

            vk_widget_get_position(dd_pop, &pp_x, &pp_y);
            vk_widget_get_metrics(dd_pop, &pp_w, &pp_h);

            px = mouse_event->x - pp_x;
            py = mouse_event->y - pp_y;

            if(px >= 0 && px < pp_w && py >= 0 && py < pp_h)
            {
                if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                {
                    if(py >= 1 && py < pp_h - 1)
                    {
                        vk_listbox_t *popup_lb = VK_LISTBOX(
                            vk_window_get_child(VK_WINDOW(dd_pop)));
                        int scroll_pos =
                            vk_listbox_get_scroll_pos(popup_lb);
                        int clicked = scroll_pos + (py - 1);
                        int count =
                            vk_listbox_get_item_count(popup_lb);

                        if(clicked >= 0 && clicked < count)
                        {
                            vk_listbox_set_curr(popup_lb, clicked);
                            dropdown_popup_detach();
                            vk_dropdown_popup_select(dd);
                            refresh_dialog();
                        }
                    }
                }

                return 0;
            }

            if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
                return 0;

            dropdown_popup_detach();
            vk_dropdown_set_expanded(dd, false);
            refresh_dialog();
            return 0;
        }
    }

    if(edit_popup != NULL)
    {
        int ep_x, ep_y, ep_w, ep_h;
        int ex, ey;

        vk_widget_get_position(VK_WIDGET(edit_popup), &ep_x, &ep_y);
        vk_widget_get_metrics(VK_WIDGET(edit_popup), &ep_w, &ep_h);

        ex = mouse_event->x - ep_x - 1;
        ey = mouse_event->y - ep_y - 1;

        if(ex < 0 || ex >= EDIT_INTERIOR_W ||
           ey < 0 || ey >= EDIT_INTERIOR_H)
            return 0;

        if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED)))
            return 0;

        /* Route the click to the field under it, in the same popup-
           interior frame (ey) the rest of this handler uses.  Each
           label+input pair spans 4 rows -- the label (1) plus the
           input's 3-row relief -- so Title is rows 0-3, Binary 4-7,
           Params 8-up.  The earlier 0-2 / 3-5 / 6-8 ranges assumed
           3-row fields, so clicks drifted to the neighbour and the
           Params field was unreachable. */
        if(ey >= 0 && ey <= 3)
        {
            edit_focus = EDIT_FOCUS_TITLE;
            update_edit_input_highlights();
            update_edit_button_highlights();
            refresh_edit_popup();
        }
        else if(ey >= 4 && ey <= 7)
        {
            edit_focus = EDIT_FOCUS_BINARY;
            update_edit_input_highlights();
            update_edit_button_highlights();
            refresh_edit_popup();
        }
        else if(ey >= 8 && ey < EDIT_INTERIOR_H - 3)
        {
            edit_focus = EDIT_FOCUS_PARAMS;
            update_edit_input_highlights();
            update_edit_button_highlights();
            refresh_edit_popup();
        }
        else if(ey >= EDIT_INTERIOR_H - 3)
        {
            int btn_count = vk_popup_get_button_count(edit_popup);
            int clicked_btn = -1;

            if(btn_count > 0)
                clicked_btn = ex / (EDIT_INTERIOR_W / btn_count);

            if(clicked_btn >= btn_count)
                clicked_btn = btn_count - 1;

            edit_focus = EDIT_FOCUS_BUTTONS;

            if(clicked_btn == EDIT_BTN_OK)
            {
                edit_active_btn = EDIT_BTN_OK;
                update_edit_input_highlights();
                update_edit_button_highlights();
                refresh_edit_popup();
                edit_popup_ok();
            }
            else if(clicked_btn == EDIT_BTN_CANCEL)
            {
                edit_active_btn = EDIT_BTN_CANCEL;
                update_edit_input_highlights();
                update_edit_button_highlights();
                refresh_edit_popup();
                edit_popup_close();
            }
            else
            {
                update_edit_input_highlights();
                update_edit_button_highlights();
                refresh_edit_popup();
            }
        }

        return 0;
    }

    vk_widget_get_position(VK_WIDGET(dialog_window), &dlg_x, &dlg_y);

    rx = mouse_event->x - dlg_x - 1;
    ry = mouse_event->y - dlg_y - 1;

    if(rx < 0 || rx >= INTERIOR_WIDTH) return 0;
    if(ry < 0 || ry >= INTERIOR_HEIGHT) return 0;

    if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED
        | BUTTON4_PRESSED | BUTTON5_PRESSED)))
        return 0;

    /*
     * Row layout — derived from INTERIOR_HEIGHT and fixed widget sizes.
     * Fixed rows: label(1)+dd(3)+label(1)+dd(3)+label(1)+dd(3)+btns(3)=15
     * Expand (listbox_frame) gets INTERIOR_HEIGHT - 15.
     */
    {
        int frame_h = INTERIOR_HEIGHT - 15;
        int lb_end = frame_h - 1;
        int cat_dd  = frame_h + 1;
        int term_dd = cat_dd + 4;
        int vis_dd  = term_dd + 4;
        int btn_row = vis_dd + 3;

    /* wheel scrolls the apps list; no other zone in this dialog reacts
       to the wheel, so a wheel hit elsewhere is dropped here.  set_prev
       / set_next move curr_item (not just the viewport), so the
       dropdowns -- which mirror the selected entry -- must be
       repopulated, the same as a click that changes selection. */
    if(bs & (BUTTON4_PRESSED | BUTTON5_PRESSED))
    {
        if(ry >= 0 && ry <= lb_end && model->count > 0)
        {
            commit_dropdowns_to_entry(model->selected);
            model->focus_zone = FOCUS_APP_LIST;

            if(bs & BUTTON4_PRESSED)
                vk_listbox_set_prev(app_listbox);
            else
                vk_listbox_set_next(app_listbox);

            model->selected = vk_listbox_get_curr(app_listbox);
            populate_dropdowns_from_entry(model->selected);

            vk_listbox_update(app_listbox);
            update_button_highlights();
            refresh_dialog();
        }
        return 0;
    }

    if(ry <= lb_end)
    {
        if(ry >= 1 && ry <= lb_end - 1)
        {
            int scroll_pos = vk_listbox_get_scroll_pos(app_listbox);
            int new_sel = scroll_pos + (ry - 1);
            struct timespec now;
            long elapsed_ms;
            bool is_dblclick = false;

            commit_dropdowns_to_entry(model->selected);
            model->focus_zone = FOCUS_APP_LIST;

            if(model->count > 0)
            {
                if(new_sel >= model->count) new_sel = model->count - 1;
                if(new_sel < 0) new_sel = 0;

                clock_gettime(CLOCK_MONOTONIC, &now);

                if(new_sel == list_last_click_item)
                {
                    elapsed_ms =
                        (now.tv_sec - list_last_click_time.tv_sec) * 1000
                        + (now.tv_nsec - list_last_click_time.tv_nsec)
                            / 1000000;

                    if(elapsed_ms >= 0 && elapsed_ms < 400)
                        is_dblclick = true;
                }

                list_last_click_time = now;
                list_last_click_item = new_sel;

                vk_listbox_set_curr(app_listbox, new_sel);
                vk_listbox_update(app_listbox);
                model->selected = new_sel;
                populate_dropdowns_from_entry(model->selected);
            }

            update_button_highlights();
            refresh_dialog();

            if(is_dblclick)
                edit_popup_open();
        }

        return 0;
    }

    if(ry >= cat_dd && ry <= cat_dd + 2)
    {
        model->focus_zone = FOCUS_CATEGORY;
        update_button_highlights();

        vk_dropdown_set_expanded(cat_dropdown, true);
        dropdown_popup_attach(cat_dropdown);

        refresh_dialog();
        return 0;
    }

    if(ry >= term_dd && ry <= term_dd + 2)
    {
        /* left column: the VTerm Mode dropdown */
        if(rx < TERM_COL_WIDTH)
        {
            model->focus_zone = FOCUS_TERMINAL;
            update_button_highlights();

            vk_dropdown_set_expanded(term_dropdown, true);
            dropdown_popup_attach(term_dropdown);

            refresh_dialog();
            return 0;
        }

        /* right column: the scrollback spinbutton.  translate the click
           into widget-local coords (the spinbutton sits flush right) and
           let it hit-test its own arrows / value field. */
        if(rx >= INTERIOR_WIDTH - SCROLLBACK_COL_WIDTH)
        {
            int local_x = rx - (INTERIOR_WIDTH - SCROLLBACK_COL_WIDTH);
            int local_y = ry - term_dd;

            model->focus_zone = FOCUS_SCROLLBACK;
            vk_spinbutton_click(scrollback_spin, local_x, local_y);
            update_button_highlights();

            refresh_dialog();
            return 0;
        }

        return 0;
    }

    if(ry >= vis_dd && ry <= vis_dd + 2)
    {
        /* left column: Visibility */
        if(rx < VIS_COL_WIDTH)
        {
            model->focus_zone = FOCUS_VISIBILITY;
            update_button_highlights();

            vk_dropdown_set_expanded(vis_dropdown, true);
            dropdown_popup_attach(vis_dropdown);

            refresh_dialog();
            return 0;
        }

        /* right column: Start directory (flush right) */
        if(rx >= INTERIOR_WIDTH - START_DIR_COL_WIDTH)
        {
            model->focus_zone = FOCUS_START_DIR;
            update_button_highlights();

            vk_dropdown_set_expanded(start_dir_dropdown, true);
            dropdown_popup_attach(start_dir_dropdown);

            refresh_dialog();
            return 0;
        }

        return 0;
    }

    if(ry >= btn_row && ry <= btn_row + 2)
    {
        int zone = -1;

        if(rx <= 4)                     zone = FOCUS_BTN_ADD;
        else if(rx >= 5 && rx <= 12)    zone = FOCUS_BTN_REMOVE;
        else if(rx >= 13 && rx <= 18)   zone = FOCUS_BTN_EDIT;
        else if(rx >= 46 && rx <= 51)   zone = FOCUS_BTN_SAVE;
        else if(rx >= 52 && rx <= 57)   zone = FOCUS_BTN_LOAD;
        else if(rx >= 58 && rx <= 65)   zone = FOCUS_BTN_CANCEL;

        if(zone >= 0)
        {
            model->focus_zone = zone;
            update_button_highlights();
            refresh_dialog();

            switch(zone)
            {
                case FOCUS_BTN_ADD:       on_add();       break;
                case FOCUS_BTN_REMOVE:    on_remove();    break;
                case FOCUS_BTN_EDIT:      on_edit();      break;
                case FOCUS_BTN_SAVE:      on_save();      break;
                case FOCUS_BTN_LOAD:      on_load();      break;
                case FOCUS_BTN_CANCEL:    on_cancel();    break;
            }
        }

        return 0;
    }

    }   /* end layout block */

    return 0;
}
