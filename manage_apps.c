#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include <libconfig.h>
#include <ncursesw/curses.h>

#include <vdk.h>

#include "vwm.h"
#include "modules.h"
#include "programs.h"
#include "profile.h"
#include "private.h"
#include "strings.h"
#include "manage_apps.h"

#define DIALOG_WIDTH        68
#define DIALOG_HEIGHT       26
#define INTERIOR_WIDTH      (DIALOG_WIDTH - 2)
#define INTERIOR_HEIGHT     (DIALOG_HEIGHT - 2)

#define MAX_ENTRIES         128
#define MAX_PARAMS          256

enum
{
    FOCUS_APP_LIST = 0,
    FOCUS_CATEGORY,
    FOCUS_TERMINAL,
    FOCUS_VISIBILITY,
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
} manage_app_entry_t;

typedef struct
{
    manage_app_entry_t  entries[MAX_ENTRIES];
    int                 count;
    int                 selected;
    int                 focus_zone;
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
static vk_listbox_t         *app_listbox = NULL;
static vk_label_t           *cat_label_widget = NULL;
static vk_dropdown_t        *cat_dropdown = NULL;
static vk_label_t           *term_label_widget = NULL;
static vk_dropdown_t        *term_dropdown = NULL;
static vk_label_t           *vis_label_widget = NULL;
static vk_dropdown_t        *vis_dropdown = NULL;
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
static vk_dropdown_t   *active_dropdown = NULL;

#define LOAD_WIDTH          50
#define LOAD_HEIGHT         20

static struct timespec      list_last_click_time;
static int                 list_last_click_item = -1;

static vk_popup_t          *load_popup = NULL;
static vk_filedialog_t     *load_filedialog = NULL;
static struct timespec     load_last_click_time;
static int                 load_last_click_item = -1;

static void listbox_rebuild(void);
static void refresh_dialog(void);
static void refresh_edit_popup(void);
static void refresh_load_popup(void);
static int  manage_apps_kmio(vk_object_t *object, int32_t keystroke);

static void
listbox_scroll_info(vk_widget_t *child,
    int *content_h, int *content_w,
    int *scroll_y, int *scroll_x)
{
    vk_listbox_t *lb = VK_LISTBOX(child);
    int metrics_w = 0;

    vk_listbox_get_metrics(lb, &metrics_w, NULL);

    if(content_h) *content_h = vk_listbox_get_item_count(lb);
    if(content_w) *content_w = metrics_w;
    if(scroll_y) *scroll_y = vk_listbox_get_curr(lb);
    if(scroll_x) *scroll_x = 0;
}

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
    config_t            cfg;
    config_setting_t    *programs;
    config_setting_t    *entry;
    const char          *str;
    int                 i = 0;

    model->count = 0;

    config_init(&cfg);

    config_read_file(&cfg, path);

    programs = config_lookup(&cfg, "programs");
    if(programs == NULL)
    {
        config_destroy(&cfg);
        return;
    }

    while(i < MAX_ENTRIES)
    {
        entry = config_setting_get_elem(programs, i);
        if(entry == NULL) break;

        manage_app_entry_t *e = &model->entries[model->count];
        memset(e, 0, sizeof(*e));

        str = NULL;
        config_setting_lookup_string(entry, "title", &str);
        if(str != NULL) strncpy(e->title, str, NAME_MAX - 1);

        str = NULL;
        config_setting_lookup_string(entry, "bin", &str);
        if(str != NULL) strncpy(e->bin, str, PATH_MAX - 1);

        str = NULL;
        config_setting_lookup_string(entry, "params", &str);
        if(str != NULL) strncpy(e->params, str, MAX_PARAMS - 1);

        str = NULL;
        config_setting_lookup_string(entry, "requires", &str);
        if(str != NULL) strncpy(e->requires, str, NAME_MAX - 1);

        str = NULL;
        config_setting_lookup_string(entry, "type", &str);
        if(str != NULL)
        {
            int val = vwm_module_type_value((char *)str);
            e->type = (val == -1) ? VWM_MOD_TYPE_MISC : val;
        }
        else
        {
            e->type = VWM_MOD_TYPE_MISC;
        }

        {
            int hidden_val = 0;
            config_setting_lookup_bool(entry, "hidden", &hidden_val);
            e->hidden = (hidden_val != 0);
        }

        model->count++;
        i++;
    }

    config_destroy(&cfg);
}

static int
model_save_to_config(const char *path)
{
    config_t            cfg;
    config_setting_t    *root;
    config_setting_t    *programs;
    config_setting_t    *entry;
    config_setting_t    *setting;
    int                 i;

    config_init(&cfg);

    config_read_file(&cfg, path);

    root = config_root_setting(&cfg);
    config_setting_remove(root, "programs");

    programs = config_setting_add(root, "programs", CONFIG_TYPE_LIST);

    for(i = 0; i < model->count; i++)
    {
        manage_app_entry_t *e = &model->entries[i];

        entry = config_setting_add(programs, NULL, CONFIG_TYPE_GROUP);

        if(e->requires[0] != '\0')
        {
            setting = config_setting_add(entry,
                "requires", CONFIG_TYPE_STRING);
            config_setting_set_string(setting, e->requires);
        }

        setting = config_setting_add(entry, "title", CONFIG_TYPE_STRING);
        config_setting_set_string(setting, e->title);

        setting = config_setting_add(entry, "bin", CONFIG_TYPE_STRING);
        config_setting_set_string(setting, e->bin);

        {
            const char *type_str = vwm_module_type_string(e->type);
            if(type_str == NULL) type_str = "Misc";
            setting = config_setting_add(entry, "type", CONFIG_TYPE_STRING);
            config_setting_set_string(setting, type_str);
        }

        if(e->params[0] != '\0')
        {
            setting = config_setting_add(entry,
                "params", CONFIG_TYPE_STRING);
            config_setting_set_string(setting, e->params);
        }

        if(e->hidden)
        {
            setting = config_setting_add(entry,
                "hidden", CONFIG_TYPE_BOOL);
            config_setting_set_bool(setting, CONFIG_TRUE);
        }
    }

    config_write_file(&cfg, path);
    config_destroy(&cfg);

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
}

static void
commit_dropdowns_to_entry(int idx)
{
    manage_app_entry_t *e;
    int cat_sel, term_sel, vis_sel;

    if(idx < 0 || idx >= model->count) return;

    e = &model->entries[idx];

    cat_sel = vk_dropdown_get_curr(cat_dropdown);
    if(cat_sel >= 0 && cat_sel < NUM_CATEGORIES)
        e->type = category_types[cat_sel];

    term_sel = vk_dropdown_get_curr(term_dropdown);
    if(term_sel >= 0 && term_sel < NUM_VTERMS)
        strncpy(e->requires, vterm_names[term_sel], NAME_MAX - 1);

    vis_sel = vk_dropdown_get_curr(vis_dropdown);
    e->hidden = (vis_sel == 1);
}

static void
listbox_rebuild(void)
{
    int i;
    int count;

    count = vk_listbox_get_item_count(app_listbox);
    for(i = count - 1; i >= 0; i--)
        vk_listbox_remove_item(app_listbox, i);

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
    vk_dropdown_t *dropdowns[] = { cat_dropdown, term_dropdown, vis_dropdown };
    int zones[] = { FOCUS_CATEGORY, FOCUS_TERMINAL, FOCUS_VISIBILITY };
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

    vk_frame_update(listbox_frame);

    for(i = 0; i < 3; i++)
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
}

/* ── dropdown popup helpers ────────────────────────────────── */

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
    vk_widget_get_position(VK_WIDGET(dropdown), &dd_x, &dd_y);
    vk_widget_get_metrics(VK_WIDGET(dropdown), &dd_w, &dd_h);
    vk_widget_get_metrics(popup, &pp_w, &pp_h);

    popup_x = dlg_x + 1 + dd_x;
    popup_y = dlg_y + 1 + dd_y + dd_h;

    if(popup_y + pp_h > scr_h)
        popup_y = dlg_y + 1 + dd_y - pp_h;

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

    refresh_dialog();
}

static void
edit_popup_ok(void)
{
    manage_app_entry_t  *e;
    const char          *text;

    if(model->selected < 0 || model->selected >= model->count)
    {
        edit_popup_close();
        return;
    }

    e = &model->entries[model->selected];

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

    edit_popup_close();
    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, model->selected);
        vk_listbox_update(app_listbox);
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
    if(model->selected < 0 || model->selected >= model->count) return;

    e = &model->entries[model->selected];
    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    edit_popup = vk_popup_create(EDIT_WIDTH, EDIT_HEIGHT,
        VK_FRAME_SINGLE, "Apply", "Cancel", NULL);
    vk_popup_set_title(edit_popup, " Edit App ");
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
    vk_input_set_relief_style(edit_input_title, VK_FRAME_SINGLE);
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
    vk_input_set_relief_style(edit_input_binary, VK_FRAME_SINGLE);
    vk_widget_set_colors(VK_WIDGET(edit_input_binary), COLOR_BLACK, COLOR_BLUE);
    vk_input_set_text(edit_input_binary, e->bin);
    vk_input_update(edit_input_binary);
    vk_box_set_widget(edit_client, 3, VK_WIDGET(edit_input_binary));

    lbl = vk_label_create(EDIT_INTERIOR_W);
    vk_label_set_text(lbl, "  Params");
    vk_widget_set_colors(VK_WIDGET(lbl), COLOR_WHITE, COLOR_BLUE);
    vk_label_update(lbl);
    vk_box_set_widget(edit_client, 4, VK_WIDGET(lbl));

    edit_input_params = vk_input_create(EDIT_INTERIOR_W);
    vk_input_set_relief_style(edit_input_params, VK_FRAME_SINGLE);
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

    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, 0);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(0);
    }

    refresh_dialog();
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
    vwm_t   *vwm;
    int     scr_width, scr_height;
    int     pos_x, pos_y;
    int     interior_w, interior_h;

    if(load_popup != NULL) return;

    load_last_click_item = -1;
    memset(&load_last_click_time, 0, sizeof(load_last_click_time));

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);

    load_popup = vk_popup_create(LOAD_WIDTH, LOAD_HEIGHT,
        VK_FRAME_SINGLE, NULL);
    vk_popup_set_title(load_popup, " Load Config ");
    vk_popup_set_border_colors(load_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(load_popup, A_BOLD);

    interior_w = LOAD_WIDTH - 2;
    interior_h = LOAD_HEIGHT - 2;

    load_filedialog = vk_filedialog_create(interior_w, interior_h,
        VK_FRAME_SINGLE, false);
    vk_filedialog_set_colors(load_filedialog, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_highlight(load_filedialog, COLOR_WHITE, COLOR_RED);
    vk_filedialog_set_button_colors(load_filedialog, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_button_attrs(load_filedialog, A_BOLD);

    {
        char dirpath[PATH_MAX];
        strncpy(dirpath, model->file_path, PATH_MAX - 1);
        dirpath[PATH_MAX - 1] = '\0';

        char *slash = strrchr(dirpath, '/');
        if(slash != NULL && slash != dirpath)
            *slash = '\0';
        else if(slash == dirpath)
            dirpath[1] = '\0';

        vk_filedialog_set_path(load_filedialog, dirpath);
    }

    vk_filedialog_update(load_filedialog);

    vk_popup_set_client(load_popup, VK_WIDGET(load_filedialog));
    vk_object_set_kmio(VK_OBJECT(load_popup), load_popup_kmio);

    pos_x = (scr_width - LOAD_WIDTH) / 2;
    pos_y = (scr_height - LOAD_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(load_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(load_popup));

    refresh_load_popup();
}

/* ── main dialog callbacks ──────────────────────────────────── */

static void
on_add(void)
{
    manage_app_entry_t *e;

    if(model->count >= MAX_ENTRIES) return;

    e = &model->entries[model->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->title, "New App", NAME_MAX - 1);
    strncpy(e->bin, "/bin/bash", PATH_MAX - 1);
    strncpy(e->requires, "vterm-color", NAME_MAX - 1);
    e->type = VWM_MOD_TYPE_TOOL;
    e->hidden = false;

    model->count++;
    model->selected = model->count - 1;

    listbox_rebuild();
    vk_listbox_set_curr(app_listbox, model->selected);
    vk_listbox_update(app_listbox);
    populate_dropdowns_from_entry(model->selected);
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

    if(model->selected >= model->count && model->count > 0)
        model->selected = model->count - 1;

    listbox_rebuild();

    if(model->count > 0)
    {
        vk_listbox_set_curr(app_listbox, model->selected);
        vk_listbox_update(app_listbox);
        populate_dropdowns_from_entry(model->selected);
    }
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
    vwm_t *vwm;

    commit_dropdowns_to_entry(model->selected);
    model_save_to_config(model->file_path);

    vwm_programs_reload();

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);

    vwm_manage_apps_close();
}

static void
on_load(void)
{
    load_popup_open();
}

static void
on_cancel(void)
{
    vwm_manage_apps_close();
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

        case FOCUS_VISIBILITY:
            retval = handle_dropdown_keys(vis_dropdown, keystroke);
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

    vk_listbox_update(app_listbox);
    vk_scroller_update(listbox_scroller);
    vk_frame_update(listbox_frame);
    vk_dropdown_update(cat_dropdown);
    vk_dropdown_update(term_dropdown);
    vk_dropdown_update(vis_dropdown);
    vk_box_update(button_hbox);
    vk_box_update(main_vbox);
    vk_window_update(dialog_window);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
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
    vk_window_set_border_style(dialog_window, VK_FRAME_SINGLE);
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

    /* non-expand rows: 1+3+1+3+1+3+3 = 15, expand gets 24-15 = 9 */
    lb_height = INTERIOR_HEIGHT - (1 + 3 + 1 + 3 + 1 + 3 + 3) - 2;

    app_listbox = vk_listbox_create(INTERIOR_WIDTH - 2, lb_height);
    vk_listbox_set_wrap(app_listbox, FALSE);
    vk_listbox_set_highlight(app_listbox, COLOR_WHITE, COLOR_RED);
    vk_widget_set_colors(VK_WIDGET(app_listbox), COLOR_WHITE, COLOR_BLACK);

    listbox_frame = vk_frame_create(INTERIOR_WIDTH, lb_height + 2);
    vk_frame_set_border_style(listbox_frame, VK_FRAME_SINGLE);
    vk_frame_set_border_colors(listbox_frame, COLOR_BLACK, COLOR_CYAN);
    vk_frame_set_child(listbox_frame, VK_WIDGET(app_listbox));
    vk_widget_set_expand(VK_WIDGET(listbox_frame));

    listbox_scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
    vk_scroller_set_border_style(listbox_scroller, VK_FRAME_SINGLE);
    vk_scroller_set_border_colors(listbox_scroller, COLOR_BLACK, COLOR_CYAN);
    vk_scroller_set_scroll_source(listbox_scroller,
        VK_WIDGET(app_listbox));
    vk_scroller_set_scroll_info(listbox_scroller,
        listbox_scroll_info);
    vk_widget_attach_scroller(VK_WIDGET(app_listbox),
        listbox_scroller);

    cat_label = vk_label_create(INTERIOR_WIDTH);
    vk_label_set_text(cat_label, "  Category");
    vk_label_set_justify(cat_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(cat_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(cat_label);
    cat_label_widget = cat_label;

    cat_dropdown = vk_dropdown_create(INTERIOR_WIDTH, 6);
    vk_dropdown_set_relief_style(cat_dropdown, VK_FRAME_SINGLE);
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

    term_label = vk_label_create(INTERIOR_WIDTH);
    vk_label_set_text(term_label, "  VTerm Mode");
    vk_label_set_justify(term_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(term_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(term_label);
    term_label_widget = term_label;

    term_dropdown = vk_dropdown_create(INTERIOR_WIDTH, 5);
    vk_dropdown_set_relief_style(term_dropdown, VK_FRAME_SINGLE);
    vk_widget_set_colors(VK_WIDGET(term_dropdown), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(term_dropdown), A_BOLD);
    vk_dropdown_set_highlight(term_dropdown, COLOR_CYAN, COLOR_BLACK);

    {
        int i;
        for(i = 0; i < NUM_VTERMS; i++)
            vk_dropdown_add_item(term_dropdown,
                (char *)vterm_labels[i], NULL, NULL);
    }

    vis_label = vk_label_create(INTERIOR_WIDTH);
    vk_label_set_text(vis_label, "  Visibility");
    vk_label_set_justify(vis_label, VK_JUSTIFY_LEFT);
    vk_widget_set_colors(VK_WIDGET(vis_label), COLOR_BLACK, COLOR_CYAN);
    vk_label_update(vis_label);
    vis_label_widget = vis_label;

    vis_dropdown = vk_dropdown_create(INTERIOR_WIDTH, 2);
    vk_dropdown_set_relief_style(vis_dropdown, VK_FRAME_SINGLE);
    vk_widget_set_colors(VK_WIDGET(vis_dropdown), COLOR_BLACK, COLOR_CYAN);
    vk_widget_set_attrs(VK_WIDGET(vis_dropdown), A_BOLD);
    vk_dropdown_set_highlight(vis_dropdown, COLOR_CYAN, COLOR_BLACK);
    vk_dropdown_add_item(vis_dropdown, "Enabled", NULL, NULL);
    vk_dropdown_add_item(vis_dropdown, "Disabled", NULL, NULL);

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
    buttons[BTN_CANCEL] = vk_button_create("Cancel");

    {
        int i;
        for(i = 0; i < NUM_BUTTONS; i++)
        {
            vk_button_set_relief_style(buttons[i], VK_FRAME_SINGLE);
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

    vk_box_set_widget(main_vbox, 0, VK_WIDGET(listbox_frame));
    vk_box_set_widget(main_vbox, 1, VK_WIDGET(cat_label));
    vk_box_set_widget(main_vbox, 2, VK_WIDGET(cat_dropdown));
    vk_box_set_widget(main_vbox, 3, VK_WIDGET(term_label));
    vk_box_set_widget(main_vbox, 4, VK_WIDGET(term_dropdown));
    vk_box_set_widget(main_vbox, 5, VK_WIDGET(vis_label));
    vk_box_set_widget(main_vbox, 6, VK_WIDGET(vis_dropdown));
    vk_box_set_widget(main_vbox, 7, VK_WIDGET(button_hbox));

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

    return 0;
}

void
vwm_manage_apps_close(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

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
    listbox_frame = NULL;
    listbox_scroller = NULL;
    app_listbox = NULL;
    cat_label_widget = NULL;
    cat_dropdown = NULL;
    term_label_widget = NULL;
    term_dropdown = NULL;
    vis_label_widget = NULL;
    vis_dropdown = NULL;
    memset(buttons, 0, sizeof(buttons));

    free(model);
    model = NULL;

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

int
vwm_manage_apps_mouse(MEVENT *mouse_event)
{
    int     dlg_x, dlg_y;
    int     rx, ry;
    mmask_t bs;

    if(dialog_window == NULL) return -1;
    if(mouse_event == NULL) return -1;

    bs = mouse_event->bstate;

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
            list_y = ly - input_h;

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

        if(ey >= 0 && ey <= 2)
        {
            edit_focus = EDIT_FOCUS_TITLE;
            update_edit_input_highlights();
            update_edit_button_highlights();
            refresh_edit_popup();
        }
        else if(ey >= 3 && ey <= 5)
        {
            edit_focus = EDIT_FOCUS_BINARY;
            update_edit_input_highlights();
            update_edit_button_highlights();
            refresh_edit_popup();
        }
        else if(ey >= 6 && ey <= 8)
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

    if(bs & BUTTON4_PRESSED)
    {
        if(model->count > 0)
        {
            commit_dropdowns_to_entry(model->selected);
            vk_listbox_set_prev(app_listbox);
            vk_listbox_update(app_listbox);
            model->selected = vk_listbox_get_curr(app_listbox);
            populate_dropdowns_from_entry(model->selected);
            model->focus_zone = FOCUS_APP_LIST;
            update_button_highlights();
            refresh_dialog();
        }
        return 0;
    }

    if(bs & BUTTON5_PRESSED)
    {
        if(model->count > 0)
        {
            commit_dropdowns_to_entry(model->selected);
            vk_listbox_set_next(app_listbox);
            vk_listbox_update(app_listbox);
            model->selected = vk_listbox_get_curr(app_listbox);
            populate_dropdowns_from_entry(model->selected);
            model->focus_zone = FOCUS_APP_LIST;
            update_button_highlights();
            refresh_dialog();
        }
        return 0;
    }

    if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
        return 0;

    /*
     * Row layout (INTERIOR_HEIGHT = 24):
     *   listbox_frame:  rows 0-8   (9h, expand, interior 7h)
     *   cat_label:      row 9      (1h)
     *   cat_dropdown:   rows 10-12 (3h)
     *   term_label:     row 13     (1h)
     *   term_dropdown:  rows 14-16 (3h)
     *   vis_label:      row 17     (1h)
     *   vis_dropdown:   rows 18-20 (3h)
     *   button_hbox:    rows 21-23 (3h)
     */

    if(ry <= 8)
    {
        if(ry >= 1 && ry <= 7)
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

    if(ry >= 10 && ry <= 12)
    {
        model->focus_zone = FOCUS_CATEGORY;
        update_button_highlights();

        vk_dropdown_set_expanded(cat_dropdown, true);
        dropdown_popup_attach(cat_dropdown);

        refresh_dialog();
        return 0;
    }

    if(ry >= 14 && ry <= 16)
    {
        model->focus_zone = FOCUS_TERMINAL;
        update_button_highlights();

        vk_dropdown_set_expanded(term_dropdown, true);
        dropdown_popup_attach(term_dropdown);

        refresh_dialog();
        return 0;
    }

    if(ry >= 18 && ry <= 20)
    {
        model->focus_zone = FOCUS_VISIBILITY;
        update_button_highlights();

        vk_dropdown_set_expanded(vis_dropdown, true);
        dropdown_popup_attach(vis_dropdown);

        refresh_dialog();
        return 0;
    }

    if(ry >= 21 && ry <= 23)
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

    return 0;
}
