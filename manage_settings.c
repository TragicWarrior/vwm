#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>

#include <libconfig.h>
#include <ncursesw/curses.h>

#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "profile.h"
#include "settings.h"
#include "panel.h"
#include "modules.h"
#include "mainmenu.h"
#include "manage_settings.h"
#include "winman.h"

#define MANAGE_SETTINGS_HELP \
"[Tab] cycle  [Up/Dn] select  " \
"[Enter] modify  [Left/Right] change value"

#define DIALOG_WIDTH        44
#define DIALOG_HEIGHT       18
#define INTERIOR_WIDTH      (DIALOG_WIDTH - 2)
#define INTERIOR_HEIGHT     (DIALOG_HEIGHT - 2)

#define NUM_SETTINGS        5
#define MAX_APP_OPTIONS     64

#define SETTING_TASK_ACTION     0
#define SETTING_DATE_ACTION     1
#define SETTING_NUM_DESKTOPS    2
#define SETTING_SCREENSAVER_CMD 3
#define SETTING_SCREENSAVER_IDLE 4

#define SETTING_TYPE_DROPDOWN   0
#define SETTING_TYPE_INPUT      1

enum
{
    FOCUS_LIST = 0,
    FOCUS_BTN_MODIFY,
    FOCUS_BTN_SAVE,
    FOCUS_BTN_LOAD,
    FOCUS_BTN_CLOSE,
    FOCUS_MAX
};

#define NUM_BUTTONS     4
#define BTN_MODIFY      0
#define BTN_SAVE        1
#define BTN_LOAD        2
#define BTN_CLOSE       3

static const struct
{
    const char  *label;
    int         type;
}
setting_defs[NUM_SETTINGS] =
{
    { "Task Indicator",     SETTING_TYPE_DROPDOWN },
    { "Date Click",         SETTING_TYPE_DROPDOWN },
    { "Desktops",           SETTING_TYPE_INPUT },
    { "Screensaver Cmd",    SETTING_TYPE_INPUT },
    { "Screensaver Idle",   SETTING_TYPE_INPUT },
};

typedef struct
{
    char    values[NUM_SETTINGS][NAME_MAX];
    int     selected;
    int     focus_zone;
    bool    dirty;
    char    file_path[PATH_MAX];

    char    app_titles[MAX_APP_OPTIONS][NAME_MAX];
    int     app_count;
}
settings_model_t;

static settings_model_t     *model = NULL;
static vk_window_t          *dialog_window = NULL;
static vk_frame_t           *listbox_frame = NULL;
static vk_scroller_t        *listbox_scroller = NULL;
static vk_box_t             *main_vbox = NULL;
static vk_box_t             *button_hbox = NULL;
static vk_listbox_t         *settings_listbox = NULL;
static vk_button_t          *buttons[NUM_BUTTONS];

#define MODIFY_DD_WIDTH     36
#define MODIFY_DD_HEIGHT    14
#define MODIFY_IN_WIDTH     32
#define MODIFY_IN_HEIGHT    9

static vk_popup_t          *modify_popup = NULL;
static vk_listbox_t        *modify_listbox = NULL;
static vk_input_t          *modify_input = NULL;
static vk_box_t            *modify_client = NULL;
static int                 modify_setting_idx = -1;
static int                 modify_active_btn = 0;
static int                 modify_focus = 0;

static struct timespec     list_last_click_time;
static int                 list_last_click_item = -1;

#define LOAD_WIDTH          50
#define LOAD_HEIGHT         20

static vk_popup_t          *load_popup = NULL;
static vk_filedialog_t     *load_filedialog = NULL;
static vk_popup_t          *warning_popup = NULL;
static vk_popup_t          *saved_popup = NULL;
static vk_popup_t          *confirm_popup = NULL;
static vk_popup_t          *save_confirm_popup = NULL;
static vk_popup_t          *error_popup = NULL;
static int                 confirm_active_btn = 0;
static int                 save_confirm_active_btn = 0;

/*
    Desktop count to apply when the tool closes.  Changing the surface
    count tears down surfaces; doing that while this dialog is attached to
    a (possibly removed) surface leaves a dangling reference and crashes on
    close.  So we persist the value at Save but defer the live add/remove
    until vwm_manage_settings_close(), after the dialog is detached.
    0 means "nothing pending".
*/
static int                 pending_desktops = 0;

static struct timespec     load_last_click_time;
static int                 load_last_click_item = -1;

static void rebuild_listbox(void);
static void refresh_dialog(void);
static void refresh_load_popup(void);
static int  manage_settings_kmio(vk_object_t *object, int32_t keystroke);

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

/* ── app options population ───────────────────────────────── */

static void
populate_app_options(void)
{
    vwm_module_t    *module;
    char            buf[NAME_MAX];
    int             type;

    model->app_count = 0;

    for(type = 0; type < VWM_MOD_TYPE_MAX; type++)
    {
        module = NULL;

        do
        {
            module = vwm_module_find_by_type(module, type);
            if(module == NULL) break;
            if(vwm_module_get_zone(module) == MODULE_ZONE_CORE) continue;
            if(vwm_module_is_hidden(module)) continue;
            if(model->app_count >= MAX_APP_OPTIONS) break;

            vwm_module_get_title(module, buf, sizeof(buf) - 1);
            strncpy(model->app_titles[model->app_count], buf, NAME_MAX - 1);
            model->app_count++;
        }
        while(module != NULL);
    }
}

/* ── model ────────────────────────────────────────────────── */

static void
model_load_from_vwm(vwm_t *vwm)
{
    strncpy(model->values[SETTING_TASK_ACTION],
        vwm->task_indicator_action, NAME_MAX - 1);
    strncpy(model->values[SETTING_DATE_ACTION],
        vwm->date_click_action, NAME_MAX - 1);
    snprintf(model->values[SETTING_NUM_DESKTOPS], NAME_MAX,
        "%d", vwm->surface_count);
    strncpy(model->values[SETTING_SCREENSAVER_CMD],
        vwm->screensaver_cmd, NAME_MAX - 1);
    snprintf(model->values[SETTING_SCREENSAVER_IDLE], NAME_MAX,
        "%d", vwm->screensaver_timeout);
}

static void
model_load_from_config(const char *path)
{
    config_t    cfg;
    const char  *str;
    int         val;

    config_init(&cfg);
    if(config_read_file(&cfg, path) != CONFIG_TRUE)
    {
        config_destroy(&cfg);
        return;
    }

    if(config_lookup_string(&cfg,
        "settings.task_indicator_action", &str) == CONFIG_TRUE)
        strncpy(model->values[SETTING_TASK_ACTION], str, NAME_MAX - 1);

    if(config_lookup_string(&cfg,
        "settings.date_click_action", &str) == CONFIG_TRUE)
        strncpy(model->values[SETTING_DATE_ACTION], str, NAME_MAX - 1);

    if(config_lookup_int(&cfg, "settings.num_desktops", &val) == CONFIG_TRUE)
        snprintf(model->values[SETTING_NUM_DESKTOPS], NAME_MAX, "%d", val);

    if(config_lookup_string(&cfg,
        "settings.screensaver_command", &str) == CONFIG_TRUE)
        strncpy(model->values[SETTING_SCREENSAVER_CMD], str, NAME_MAX - 1);

    if(config_lookup_int(&cfg,
        "settings.screensaver_timeout", &val) == CONFIG_TRUE)
        snprintf(model->values[SETTING_SCREENSAVER_IDLE], NAME_MAX, "%d", val);

    config_destroy(&cfg);
}

static void
commit_to_vwm(void)
{
    vwm_t *vwm = vwm_get_instance();

    strncpy(vwm->task_indicator_action,
        model->values[SETTING_TASK_ACTION], NAME_MAX - 1);
    vwm->task_indicator_action[NAME_MAX - 1] = '\0';

    strncpy(vwm->date_click_action,
        model->values[SETTING_DATE_ACTION], NAME_MAX - 1);
    vwm->date_click_action[NAME_MAX - 1] = '\0';

    strncpy(vwm->screensaver_cmd,
        model->values[SETTING_SCREENSAVER_CMD], NAME_MAX - 1);
    vwm->screensaver_cmd[NAME_MAX - 1] = '\0';

    vwm->screensaver_timeout = atoi(model->values[SETTING_SCREENSAVER_IDLE]);
    if(vwm->screensaver_timeout < 0) vwm->screensaver_timeout = 0;
}

/* ── listbox rendering ────────────────────────────────────── */

static void
rebuild_listbox(void)
{
    int i;
    int count;

    count = vk_listbox_get_item_count(settings_listbox);
    for(i = count - 1; i >= 0; i--)
        vk_listbox_remove_item(settings_listbox, i);

    for(i = 0; i < NUM_SETTINGS; i++)
    {
        char display[NAME_MAX + 128];
        char dots[64];
        int label_len = strlen(setting_defs[i].label);
        int value_len = strlen(model->values[i]);
        int item_w = INTERIOR_WIDTH - 4;
        int dot_count = item_w - label_len - value_len - 5;

        if(dot_count < 2) dot_count = 2;
        if(dot_count > (int)sizeof(dots) - 1) dot_count = sizeof(dots) - 1;

        memset(dots, '.', dot_count);
        dots[dot_count] = '\0';

        snprintf(display, sizeof(display), "%s %s [%s]",
            setting_defs[i].label, dots, model->values[i]);

        vk_listbox_add_item(settings_listbox, display, NULL, NULL);
    }

    vk_listbox_update(settings_listbox);
}

/* ── value cycling for dropdown settings ──────────────────── */

static void
cycle_value(int setting_idx, int direction)
{
    int i, curr, total;

    if(setting_idx == SETTING_TASK_ACTION)
    {
        total = model->app_count + 1;
        curr = 0;

        for(i = 0; i < model->app_count; i++)
        {
            if(strcmp(model->values[setting_idx],
                model->app_titles[i]) == 0)
            {
                curr = i + 1;
                break;
            }
        }

        curr += direction;
        if(curr < 0) curr = total - 1;
        if(curr >= total) curr = 0;

        if(curr == 0)
            strncpy(model->values[setting_idx], "none", NAME_MAX - 1);
        else
            strncpy(model->values[setting_idx],
                model->app_titles[curr - 1], NAME_MAX - 1);
    }
    else if(setting_idx == SETTING_DATE_ACTION)
    {
        total = model->app_count + 1;
        curr = 0;

        for(i = 0; i < model->app_count; i++)
        {
            if(strcmp(model->values[setting_idx],
                model->app_titles[i]) == 0)
            {
                curr = i + 1;
                break;
            }
        }

        curr += direction;
        if(curr < 0) curr = total - 1;
        if(curr >= total) curr = 0;

        if(curr == 0)
            strncpy(model->values[setting_idx], "calendar", NAME_MAX - 1);
        else
            strncpy(model->values[setting_idx],
                model->app_titles[curr - 1], NAME_MAX - 1);
    }

    model->dirty = true;
    rebuild_listbox();
    vk_listbox_set_curr(settings_listbox, model->selected);
    vk_listbox_update(settings_listbox);
    refresh_dialog();
}

/* ── modify popup ────────────────────────────────────────────── */

static void
modify_popup_close(void)
{
    vwm_t *vwm;

    if(modify_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(modify_popup));

    vk_popup_destroy(modify_popup);
    modify_popup = NULL;
    modify_listbox = NULL;
    modify_input = NULL;
    modify_client = NULL;
    modify_setting_idx = -1;

    refresh_dialog();
}

static void
modify_popup_apply(void)
{
    if(modify_popup == NULL) return;

    if(modify_setting_idx < 0 || modify_setting_idx >= NUM_SETTINGS) return;

    if(setting_defs[modify_setting_idx].type == SETTING_TYPE_DROPDOWN)
    {
        if(modify_listbox != NULL)
        {
            int curr = vk_listbox_get_curr(modify_listbox);
            int count = vk_listbox_get_item_count(modify_listbox);

            if(curr >= 0 && curr < count)
            {
                if(modify_setting_idx == SETTING_TASK_ACTION)
                {
                    if(curr == 0)
                        strncpy(model->values[modify_setting_idx],
                            "none", NAME_MAX - 1);
                    else
                        strncpy(model->values[modify_setting_idx],
                            model->app_titles[curr - 1], NAME_MAX - 1);
                }
                else if(modify_setting_idx == SETTING_DATE_ACTION)
                {
                    if(curr == 0)
                        strncpy(model->values[modify_setting_idx],
                            "calendar", NAME_MAX - 1);
                    else
                        strncpy(model->values[modify_setting_idx],
                            model->app_titles[curr - 1], NAME_MAX - 1);
                }
            }
        }
    }
    else if(setting_defs[modify_setting_idx].type == SETTING_TYPE_INPUT)
    {
        if(modify_input != NULL)
        {
            const char *text = vk_input_get_text(modify_input);
            if(text != NULL)
            {
                strncpy(model->values[modify_setting_idx],
                    text, NAME_MAX - 1);
                model->values[modify_setting_idx][NAME_MAX - 1] = '\0';
            }
        }
    }

    model->dirty = true;
    modify_popup_close();
    rebuild_listbox();
    vk_listbox_set_curr(settings_listbox, model->selected);
    vk_listbox_update(settings_listbox);
    refresh_dialog();
}

static void
update_modify_button_highlights(void)
{
    int count;
    int i;

    if(modify_popup == NULL) return;

    count = vk_popup_get_button_count(modify_popup);

    for(i = 0; i < count; i++)
    {
        vk_button_t *btn = vk_popup_get_button(modify_popup, i);

        vk_button_release(btn);

        if(modify_focus == 1 && i == modify_active_btn)
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
update_modify_input_highlight(void)
{
    if(modify_popup == NULL) return;
    if(modify_input == NULL) return;

    if(modify_focus == 0)
    {
        vk_widget_set_colors(VK_WIDGET(modify_input),
            COLOR_CYAN, COLOR_BLUE);
        vk_input_show_cursor(modify_input, true);
    }
    else
    {
        vk_widget_set_colors(VK_WIDGET(modify_input),
            COLOR_WHITE, COLOR_BLUE);
        vk_input_show_cursor(modify_input, false);
    }

    vk_input_update(modify_input);
}

static void
refresh_modify_popup(void)
{
    vwm_t *vwm;

    if(modify_popup == NULL) return;

    if(modify_client != NULL)
        vk_box_update(modify_client);

    vk_popup_update(modify_popup);

    vwm = vwm_get_instance();
    vk_screen_refresh(vwm->screen);
}

static int
modify_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        modify_popup_close();
        return 0;
    }

    if(keystroke == '\t')
    {
        if(modify_focus == 1 && modify_active_btn == 0)
        {
            modify_active_btn = 1;
        }
        else
        {
            modify_focus = (modify_focus + 1) % 2;
            if(modify_focus == 1)
                modify_active_btn = 0;
        }

        update_modify_button_highlights();
        update_modify_input_highlight();
        refresh_modify_popup();
        return 0;
    }

    if(modify_focus == 0)
    {
        if(setting_defs[modify_setting_idx].type == SETTING_TYPE_DROPDOWN
            && modify_listbox != NULL)
        {
            if(keystroke == KEY_UP)
            {
                vk_listbox_set_prev(modify_listbox);
                vk_listbox_update(modify_listbox);
                refresh_modify_popup();
                return 0;
            }

            if(keystroke == KEY_DOWN)
            {
                vk_listbox_set_next(modify_listbox);
                vk_listbox_update(modify_listbox);
                refresh_modify_popup();
                return 0;
            }

            if(keystroke == KEY_CRLF || keystroke == ' ')
            {
                modify_popup_apply();
                return 0;
            }
        }
        else if(setting_defs[modify_setting_idx].type == SETTING_TYPE_INPUT
            && modify_input != NULL)
        {
            if(keystroke == KEY_CRLF)
            {
                modify_popup_apply();
                return 0;
            }

            if(keystroke == KEY_LEFT)
            {
                vk_input_move_cursor(modify_input, -1);
                vk_input_update(modify_input);
                refresh_modify_popup();
                return 0;
            }

            if(keystroke == KEY_RIGHT)
            {
                vk_input_move_cursor(modify_input, 1);
                vk_input_update(modify_input);
                refresh_modify_popup();
                return 0;
            }

            if(keystroke == KEY_BACKSPACE || keystroke == 127)
            {
                vk_input_backspace(modify_input);
                vk_input_update(modify_input);
                refresh_modify_popup();
                return 0;
            }

            if(keystroke == KEY_DC)
            {
                vk_input_delete(modify_input);
                vk_input_update(modify_input);
                refresh_modify_popup();
                return 0;
            }

            {
                int numeric = (modify_setting_idx == SETTING_NUM_DESKTOPS ||
                    modify_setting_idx == SETTING_SCREENSAVER_IDLE);
                int accept = numeric ? (isdigit(keystroke) != 0)
                    : (keystroke >= 32 && keystroke <= 126);

                if(accept)
                {
                    vk_input_insert_char(modify_input, keystroke);
                    vk_input_update(modify_input);
                    refresh_modify_popup();
                    return 0;
                }
            }
        }

        return 0;
    }

    if(modify_focus == 1)
    {
        if(keystroke == KEY_LEFT || keystroke == KEY_RIGHT)
        {
            modify_active_btn = (modify_active_btn == 0) ? 1 : 0;
            update_modify_button_highlights();
            refresh_modify_popup();
            return 0;
        }

        if(keystroke == KEY_CRLF || keystroke == ' ')
        {
            if(modify_active_btn == 0)
                modify_popup_apply();
            else
                modify_popup_close();
            return 0;
        }
    }

    return 0;
}

static void
modify_popup_open(int setting_idx)
{
    vwm_t       *vwm;
    int         scr_w, scr_h;
    int         pos_x, pos_y;
    int         popup_w, popup_h;
    char        title[64];

    if(modify_popup != NULL) return;
    if(setting_idx < 0 || setting_idx >= NUM_SETTINGS) return;

    modify_setting_idx = setting_idx;
    modify_focus = 0;
    modify_active_btn = 0;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    snprintf(title, sizeof(title), " Modify: %s ",
        setting_defs[setting_idx].label);

    if(setting_defs[setting_idx].type == SETTING_TYPE_DROPDOWN)
    {
        int i, sel_idx = 0;

        popup_w = MODIFY_DD_WIDTH;
        popup_h = MODIFY_DD_HEIGHT;

        modify_popup = vk_popup_create(popup_w, popup_h,
            VK_FRAME_SINGLE, "Apply", "Cancel", NULL);
        vk_popup_set_title(modify_popup, title);
        vk_popup_set_border_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_border_attrs(modify_popup, A_BOLD);
        vk_popup_set_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_button_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_button_attrs(modify_popup, A_BOLD);

        modify_listbox = vk_listbox_create(popup_w - 2, popup_h - 5);
        vk_listbox_set_wrap(modify_listbox, FALSE);
        vk_listbox_set_highlight(modify_listbox, COLOR_WHITE, COLOR_RED);
        vk_widget_set_colors(VK_WIDGET(modify_listbox),
            COLOR_WHITE, COLOR_BLUE);

        if(setting_idx == SETTING_TASK_ACTION)
        {
            vk_listbox_add_item(modify_listbox, "none", NULL, NULL);

            for(i = 0; i < model->app_count; i++)
            {
                vk_listbox_add_item(modify_listbox,
                    model->app_titles[i], NULL, NULL);
                if(strcmp(model->values[setting_idx],
                    model->app_titles[i]) == 0)
                    sel_idx = i + 1;
            }
        }
        else if(setting_idx == SETTING_DATE_ACTION)
        {
            vk_listbox_add_item(modify_listbox,
                "Built-in Calendar", NULL, NULL);

            for(i = 0; i < model->app_count; i++)
            {
                vk_listbox_add_item(modify_listbox,
                    model->app_titles[i], NULL, NULL);
                if(strcmp(model->values[setting_idx],
                    model->app_titles[i]) == 0)
                    sel_idx = i + 1;
            }
        }

        vk_listbox_set_curr(modify_listbox, sel_idx);
        vk_listbox_update(modify_listbox);

        vk_popup_set_client(modify_popup, VK_WIDGET(modify_listbox));
    }
    else if(setting_defs[setting_idx].type == SETTING_TYPE_INPUT)
    {
        vk_label_t  *lbl;

        popup_w = MODIFY_IN_WIDTH;
        popup_h = MODIFY_IN_HEIGHT;

        modify_popup = vk_popup_create(popup_w, popup_h,
            VK_FRAME_SINGLE, "Apply", "Cancel", NULL);
        vk_popup_set_title(modify_popup, title);
        vk_popup_set_border_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_border_attrs(modify_popup, A_BOLD);
        vk_popup_set_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_button_colors(modify_popup, COLOR_WHITE, COLOR_BLUE);
        vk_popup_set_button_attrs(modify_popup, A_BOLD);

        modify_client = vk_box_create(popup_w - 2, popup_h - 5,
            VK_BOX_VERTICAL, 2);
        vk_box_set_homogeneous(modify_client, false);
        vk_widget_set_colors(VK_WIDGET(modify_client),
            COLOR_WHITE, COLOR_BLUE);

        lbl = vk_label_create(popup_w - 2);
        {
            const char *prompt = "  Value:";
            if(setting_idx == SETTING_NUM_DESKTOPS)
                prompt = "  Number of Desktops (2-6):";
            else if(setting_idx == SETTING_SCREENSAVER_CMD)
                prompt = "  Screensaver command:";
            else if(setting_idx == SETTING_SCREENSAVER_IDLE)
                prompt = "  Idle minutes (0 = off):";
            vk_label_set_text(lbl, prompt);
        }
        vk_widget_set_colors(VK_WIDGET(lbl), COLOR_WHITE, COLOR_BLUE);
        vk_label_update(lbl);
        vk_box_set_widget(modify_client, 0, VK_WIDGET(lbl));

        modify_input = vk_input_create(popup_w - 4);
        vk_input_set_relief_style(modify_input, VK_FRAME_SINGLE);
        vk_widget_set_colors(VK_WIDGET(modify_input),
            COLOR_CYAN, COLOR_BLUE);
        vk_input_set_text(modify_input,
            model->values[setting_idx]);
        vk_input_show_cursor(modify_input, true);
        vk_input_update(modify_input);
        vk_box_set_widget(modify_client, 1, VK_WIDGET(modify_input));

        vk_popup_set_client(modify_popup, VK_WIDGET(modify_client));
    }
    else
    {
        return;
    }

    vk_object_set_kmio(VK_OBJECT(modify_popup), modify_popup_kmio);
    update_modify_button_highlights();
    update_modify_input_highlight();

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(modify_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(modify_popup));

    refresh_modify_popup();
}

static void
on_modify(void)
{
    modify_popup_open(model->selected);
}

/* ── highlight helpers ────────────────────────────────────── */

static void
update_button_highlights(void)
{
    int focus_zones[] =
    {
        FOCUS_BTN_MODIFY, FOCUS_BTN_SAVE, FOCUS_BTN_LOAD, FOCUS_BTN_CLOSE
    };
    int btn_indices[] =
    {
        BTN_MODIFY, BTN_SAVE, BTN_LOAD, BTN_CLOSE
    };
    int i;

    for(i = 0; i < NUM_BUTTONS; i++)
    {
        if(model->focus_zone == focus_zones[i])
        {
            vk_widget_set_colors(VK_WIDGET(buttons[btn_indices[i]]),
                COLOR_YELLOW, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(buttons[btn_indices[i]]), A_BOLD);
        }
        else
        {
            vk_widget_set_colors(VK_WIDGET(buttons[btn_indices[i]]),
                COLOR_BLACK, COLOR_CYAN);
            vk_widget_set_attrs(VK_WIDGET(buttons[btn_indices[i]]), A_BOLD);
        }

        vk_button_release(buttons[btn_indices[i]]);
        vk_button_update(buttons[btn_indices[i]]);
    }

    if(model->focus_zone == FOCUS_LIST)
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
}

static void
refresh_dialog(void)
{
    vwm_t *vwm = vwm_get_instance();

    vk_listbox_update(settings_listbox);
    vk_scroller_update(listbox_scroller);
    vk_frame_update(listbox_frame);
    update_button_highlights();
    vk_box_update(button_hbox);
    vk_box_update(main_vbox);
    vk_window_update(dialog_window);
    vk_screen_refresh(vwm->screen);
}

/* ── error popup ──────────────────────────────────────────── */

static void
error_popup_close(void)
{
    vwm_t *vwm;

    if(error_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(error_popup));

    vk_popup_destroy(error_popup);
    error_popup = NULL;

    refresh_dialog();
}

static int
error_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27 || keystroke == KEY_CRLF || keystroke == ' ')
    {
        error_popup_close();
        return 0;
    }

    return 0;
}

static void
error_popup_show(const char *msg)
{
    vwm_t       *vwm;
    vk_box_t    *client;
    vk_label_t  *label;
    int         scr_w, scr_h;
    int         popup_w = 40;
    int         popup_h = 7;
    int         pos_x, pos_y;

    if(error_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    error_popup = vk_popup_create(popup_w, popup_h,
        VK_FRAME_SINGLE, "OK", NULL);
    vk_popup_set_title(error_popup, " Error ");
    vk_popup_set_border_colors(error_popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(error_popup, A_NORMAL);
    {
        vk_box_t *bar = vk_popup_get_button_bar(error_popup);
        if(bar != NULL)
        {
            vk_widget_set_colors(VK_WIDGET(bar), COLOR_RED, COLOR_WHITE);
            vk_widget_fill(VK_WIDGET(bar),
                ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
        }
    }

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 1);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_RED, COLOR_WHITE);

    label = vk_label_create(popup_w - 2);
    vk_label_set_justify(label, VK_JUSTIFY_CENTER);
    vk_label_set_text(label, msg);
    vk_widget_set_colors(VK_WIDGET(label), COLOR_RED, COLOR_WHITE);
    vk_label_update(label);
    vk_box_set_widget(client, 0, VK_WIDGET(label));

    vk_popup_set_client(error_popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_popup_set_colors(error_popup, COLOR_RED, COLOR_WHITE);
    vk_object_set_kmio(VK_OBJECT(error_popup), error_popup_kmio);

    {
        vk_button_t *ok_btn = vk_popup_get_button(error_popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(error_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(error_popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(error_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── warning popup ────────────────────────────────────────── */

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
    vwm_t       *vwm;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 40;
    int         popup_h = 9;
    int         pos_x, pos_y;

    if(warning_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    warning_popup = vk_popup_create(popup_w, popup_h,
        VK_FRAME_SINGLE, "OK", NULL);
    vk_popup_set_title(warning_popup, " Warning ");
    vk_popup_set_border_colors(warning_popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(warning_popup, A_NORMAL);
    {
        vk_box_t *bar = vk_popup_get_button_bar(warning_popup);
        if(bar != NULL)
        {
            vk_widget_set_colors(VK_WIDGET(bar), COLOR_RED, COLOR_WHITE);
            vk_widget_fill(VK_WIDGET(bar),
                ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
        }
    }

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 4);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_RED, COLOR_WHITE);

    {
        vk_filler_t *top_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(top_pad), COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 0, VK_WIDGET(top_pad));

        vk_label_t *line1 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line1, VK_JUSTIFY_CENTER);
        vk_label_set_text(line1,
            "If the terminal becomes too small, this dialog");
        vk_widget_set_colors(VK_WIDGET(line1), COLOR_RED, COLOR_WHITE);
        vk_label_update(line1);
        vk_box_set_widget(client, 1, VK_WIDGET(line1));

        vk_label_t *line2 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line2, VK_JUSTIFY_CENTER);
        vk_label_set_text(line2,
            "will close and unsaved changes may be lost.");
        vk_widget_set_colors(VK_WIDGET(line2), COLOR_RED, COLOR_WHITE);
        vk_label_update(line2);
        vk_box_set_widget(client, 2, VK_WIDGET(line2));

        vk_filler_t *bot_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(bot_pad), COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 3, VK_WIDGET(bot_pad));
    }

    vk_popup_set_client(warning_popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_popup_set_colors(warning_popup, COLOR_RED, COLOR_WHITE);
    vk_object_set_kmio(VK_OBJECT(warning_popup), warning_popup_kmio);

    {
        vk_button_t *ok_btn = vk_popup_get_button(warning_popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(warning_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(warning_popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(warning_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── saved popup ──────────────────────────────────────────── */

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
    vwm_t       *vwm;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 30;
    int         popup_h = 7;
    int         pos_x, pos_y;

    if(saved_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    saved_popup = vk_popup_create(popup_w, popup_h,
        VK_FRAME_SINGLE, "OK", NULL);
    vk_popup_set_border_colors(saved_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(saved_popup, A_BOLD);
    vk_popup_set_colors(saved_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_button_colors(saved_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_button_attrs(saved_popup, A_BOLD);

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 1);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_WHITE, COLOR_BLUE);

    {
        vk_label_t *label = vk_label_create(popup_w - 2);
        vk_label_set_justify(label, VK_JUSTIFY_CENTER);
        vk_label_set_text(label, "Settings saved.");
        vk_widget_set_colors(VK_WIDGET(label), COLOR_WHITE, COLOR_BLUE);
        vk_label_update(label);
        vk_box_set_widget(client, 0, VK_WIDGET(label));
    }

    vk_popup_set_client(saved_popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(saved_popup), saved_popup_kmio);

    {
        vk_button_t *ok_btn = vk_popup_get_button(saved_popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_BLUE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(saved_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(saved_popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
    vk_box_update(client);
    vk_popup_update(saved_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── confirm-discard popup ────────────────────────────────── */

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
            vwm_manage_settings_close();
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
    vwm_t       *vwm;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 40;
    int         popup_h = 9;
    int         pos_x, pos_y;

    if(confirm_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    confirm_popup = vk_popup_create(popup_w, popup_h,
        VK_FRAME_SINGLE, "Discard", "Cancel", NULL);
    vk_popup_set_title(confirm_popup, " Confirm ");
    vk_popup_set_border_colors(confirm_popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(confirm_popup, A_NORMAL);
    vk_popup_set_colors(confirm_popup, COLOR_RED, COLOR_WHITE);
    {
        vk_box_t *bar = vk_popup_get_button_bar(confirm_popup);
        if(bar != NULL)
        {
            vk_widget_set_colors(VK_WIDGET(bar), COLOR_RED, COLOR_WHITE);
            vk_widget_fill(VK_WIDGET(bar),
                ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
        }
    }

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 4);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_RED, COLOR_WHITE);

    {
        vk_filler_t *top_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(top_pad), COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 0, VK_WIDGET(top_pad));

        vk_label_t *line1 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line1, VK_JUSTIFY_CENTER);
        vk_label_set_text(line1, "You have unsaved changes.");
        vk_widget_set_colors(VK_WIDGET(line1), COLOR_RED, COLOR_WHITE);
        vk_label_update(line1);
        vk_box_set_widget(client, 1, VK_WIDGET(line1));

        vk_label_t *line2 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line2, VK_JUSTIFY_CENTER);
        vk_label_set_text(line2, "Discard changes and close?");
        vk_widget_set_colors(VK_WIDGET(line2), COLOR_RED, COLOR_WHITE);
        vk_label_update(line2);
        vk_box_set_widget(client, 2, VK_WIDGET(line2));

        vk_filler_t *bot_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(bot_pad), COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 3, VK_WIDGET(bot_pad));
    }

    vk_popup_set_client(confirm_popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(confirm_popup), confirm_popup_kmio);

    confirm_active_btn = 0;

    {
        int count = vk_popup_get_button_count(confirm_popup);
        for(int i = 0; i < count; i++)
        {
            vk_button_t *btn = vk_popup_get_button(confirm_popup, i);

            if(i == confirm_active_btn)
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_YELLOW, COLOR_WHITE);
            }
            else
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_BLACK, COLOR_WHITE);
            }

            vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
            vk_button_update(btn);
        }
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(confirm_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(confirm_popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(confirm_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── save-confirm popup ──────────────────────────────────── */

static void do_save(void);

static void
save_confirm_popup_close(void)
{
    vwm_t *vwm;

    if(save_confirm_popup == NULL) return;

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(save_confirm_popup));

    vk_popup_destroy(save_confirm_popup);
    save_confirm_popup = NULL;

    refresh_dialog();
}

static int
save_confirm_popup_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(keystroke == 27)
    {
        save_confirm_popup_close();
        return 0;
    }

    if(keystroke == '\t' || keystroke == KEY_RIGHT || keystroke == KEY_LEFT)
    {
        int count = vk_popup_get_button_count(save_confirm_popup);
        save_confirm_active_btn =
            (save_confirm_active_btn + 1) % count;

        for(int i = 0; i < count; i++)
        {
            vk_button_t *btn =
                vk_popup_get_button(save_confirm_popup, i);
            vk_button_release(btn);

            if(i == save_confirm_active_btn)
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

        vk_popup_update(save_confirm_popup);

        vwm_t *vwm = vwm_get_instance();
        vk_screen_refresh(vwm->screen);
        return 0;
    }

    if(keystroke == KEY_CRLF || keystroke == ' ')
    {
        if(save_confirm_active_btn == 0)
        {
            save_confirm_popup_close();
            do_save();
        }
        else
        {
            save_confirm_popup_close();
        }

        return 0;
    }

    return 0;
}

static void
save_confirm_popup_show(void)
{
    vwm_t       *vwm;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 40;
    int         popup_h = 9;
    int         pos_x, pos_y;

    if(save_confirm_popup != NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    save_confirm_popup = vk_popup_create(popup_w, popup_h,
        VK_FRAME_SINGLE, "Save", "Cancel", NULL);
    vk_popup_set_title(save_confirm_popup, " Confirm Save ");
    vk_popup_set_border_colors(save_confirm_popup,
        COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(save_confirm_popup, A_NORMAL);
    vk_popup_set_colors(save_confirm_popup, COLOR_RED, COLOR_WHITE);
    {
        vk_box_t *bar =
            vk_popup_get_button_bar(save_confirm_popup);
        if(bar != NULL)
        {
            vk_widget_set_colors(VK_WIDGET(bar),
                COLOR_RED, COLOR_WHITE);
            vk_widget_fill(VK_WIDGET(bar),
                ' ' | COLOR_PAIR(
                    vdk_color_pair(COLOR_RED, COLOR_WHITE)));
        }
    }

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 4);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_RED, COLOR_WHITE);

    {
        vk_filler_t *top_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(top_pad),
            COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 0, VK_WIDGET(top_pad));

        vk_label_t *line1 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line1, VK_JUSTIFY_CENTER);
        vk_label_set_text(line1, "Save current settings?");
        vk_widget_set_colors(VK_WIDGET(line1),
            COLOR_RED, COLOR_WHITE);
        vk_label_update(line1);
        vk_box_set_widget(client, 1, VK_WIDGET(line1));

        vk_label_t *line2 = vk_label_create(popup_w - 2);
        vk_label_set_justify(line2, VK_JUSTIFY_CENTER);
        vk_label_set_text(line2, "Changes will take effect now.");
        vk_widget_set_colors(VK_WIDGET(line2),
            COLOR_RED, COLOR_WHITE);
        vk_label_update(line2);
        vk_box_set_widget(client, 2, VK_WIDGET(line2));

        vk_filler_t *bot_pad = vk_filler_create();
        vk_widget_set_colors(VK_WIDGET(bot_pad),
            COLOR_RED, COLOR_WHITE);
        vk_box_set_widget(client, 3, VK_WIDGET(bot_pad));
    }

    vk_popup_set_client(save_confirm_popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client),
            st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(save_confirm_popup),
        save_confirm_popup_kmio);

    save_confirm_active_btn = 0;

    {
        int count =
            vk_popup_get_button_count(save_confirm_popup);
        for(int i = 0; i < count; i++)
        {
            vk_button_t *btn =
                vk_popup_get_button(save_confirm_popup, i);

            if(i == save_confirm_active_btn)
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_YELLOW, COLOR_WHITE);
            }
            else
            {
                vk_widget_set_colors(VK_WIDGET(btn),
                    COLOR_BLACK, COLOR_WHITE);
            }

            vk_widget_set_attrs(VK_WIDGET(btn), A_BOLD);
            vk_button_update(btn);
        }
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(save_confirm_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(save_confirm_popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(save_confirm_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── load popup ───────────────────────────────────────────── */

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

    if(path == NULL || selected == NULL) return;

    if(strcmp(path, "/") == 0)
        snprintf(fullpath, sizeof(fullpath), "/%s", selected);
    else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, selected);

    strncpy(model->file_path, fullpath, PATH_MAX - 1);
    model->file_path[PATH_MAX - 1] = '\0';

    load_popup_close();

    model_load_from_config(model->file_path);
    model->dirty = false;

    rebuild_listbox();

    if(NUM_SETTINGS > 0)
    {
        vk_listbox_set_curr(settings_listbox, 0);
        vk_listbox_update(settings_listbox);
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
    vwm_t       *vwm;
    int         scr_w, scr_h;
    int         interior_w, interior_h;
    int         pos_x, pos_y;

    if(load_popup != NULL) return;

    load_last_click_item = -1;
    memset(&load_last_click_time, 0, sizeof(load_last_click_time));

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    load_popup = vk_popup_create(LOAD_WIDTH, LOAD_HEIGHT,
        VK_FRAME_SINGLE, NULL);
    vk_popup_set_title(load_popup, " Load Settings ");
    vk_popup_set_border_colors(load_popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(load_popup, A_BOLD);

    interior_w = LOAD_WIDTH - 2;
    interior_h = LOAD_HEIGHT - 2;

    load_filedialog = vk_filedialog_create(interior_w, interior_h,
        VK_FRAME_SINGLE, false);
    vk_filedialog_set_colors(load_filedialog, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_highlight(load_filedialog, COLOR_WHITE, COLOR_RED);
    vk_filedialog_set_button_colors(load_filedialog,
        COLOR_WHITE, COLOR_BLUE);
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

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(load_filedialog));
        vk_widget_set_state(VK_WIDGET(load_filedialog),
            st & ~VK_STATE_EXPAND);
    }

    vk_object_set_kmio(VK_OBJECT(load_popup), load_popup_kmio);

    pos_x = (scr_w - LOAD_WIDTH) / 2;
    pos_y = (scr_h - LOAD_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(load_popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(load_popup));

    vk_popup_update(load_popup);
    vk_screen_refresh(vwm->screen);
}

/* ── actions ──────────────────────────────────────────────── */

static void
do_save(void)
{
    vwm_t   *vwm = vwm_get_instance();
    int     new_count;

    new_count = atoi(model->values[SETTING_NUM_DESKTOPS]);

    if(new_count < 2 || new_count > 6)
    {
        error_popup_show("Desktops must be between 2 and 6.");
        return;
    }

    commit_to_vwm();

    /*
        Persist the new desktop count now, but defer the actual surface
        add/remove until the tool closes (applying it here would tear down
        the surface this dialog lives on).  vwm_settings_save_general()
        writes vwm->surface_count, so set it across the save then restore
        it; vwm_apply_surface_count() in close uses it as the old count.
    */
    {
        int real_count = vwm->surface_count;

        vwm->surface_count = new_count;
        vwm_settings_save_general(vwm);
        vwm->surface_count = real_count;
    }

    pending_desktops = new_count;
    model->dirty = false;

    saved_popup_show();
}

static void
on_save(void)
{
    int new_count = atoi(model->values[SETTING_NUM_DESKTOPS]);

    if(new_count < 2 || new_count > 6)
    {
        error_popup_show("Desktops must be between 2 and 6.");
        return;
    }

    save_confirm_popup_show();
}

static void
on_load(void)
{
    load_popup_open();
}

static void
on_close(void)
{
    if(model->dirty)
    {
        confirm_popup_show();
        return;
    }

    vwm_manage_settings_close();
}

/* ── keyboard handler ─────────────────────────────────────── */

static int
manage_settings_kmio(vk_object_t *object, int32_t keystroke)
{
    (void)object;

    if(confirm_popup != NULL)
        return confirm_popup_kmio(NULL, keystroke);

    if(save_confirm_popup != NULL)
        return save_confirm_popup_kmio(NULL, keystroke);

    if(saved_popup != NULL)
        return saved_popup_kmio(NULL, keystroke);

    if(error_popup != NULL)
        return error_popup_kmio(NULL, keystroke);

    if(warning_popup != NULL)
        return warning_popup_kmio(NULL, keystroke);

    if(modify_popup != NULL)
        return modify_popup_kmio(NULL, keystroke);

    if(load_popup != NULL)
        return load_popup_kmio(NULL, keystroke);

    if(keystroke == 27)
    {
        on_close();
        return 0;
    }

    if(keystroke == '\t')
    {
        model->focus_zone = (model->focus_zone + 1) % FOCUS_MAX;
        update_button_highlights();
        refresh_dialog();
        return 0;
    }

    if(model->focus_zone == FOCUS_LIST)
    {
        if(keystroke == KEY_UP)
        {
            if(model->selected > 0)
            {
                model->selected--;
                vk_listbox_set_curr(settings_listbox, model->selected);
                vk_listbox_update(settings_listbox);
                refresh_dialog();
            }
            return 0;
        }

        if(keystroke == KEY_DOWN)
        {
            if(model->selected < NUM_SETTINGS - 1)
            {
                model->selected++;
                vk_listbox_set_curr(settings_listbox, model->selected);
                vk_listbox_update(settings_listbox);
                refresh_dialog();
            }
            return 0;
        }

        if(keystroke == KEY_LEFT)
        {
            if(setting_defs[model->selected].type == SETTING_TYPE_DROPDOWN)
                cycle_value(model->selected, -1);
            return 0;
        }

        if(keystroke == KEY_RIGHT)
        {
            if(setting_defs[model->selected].type == SETTING_TYPE_DROPDOWN)
                cycle_value(model->selected, 1);
            return 0;
        }

        if(keystroke == KEY_CRLF || keystroke == ' ')
        {
            on_modify();
            return 0;
        }

        return 0;
    }

    if(keystroke == KEY_CRLF || keystroke == ' ')
    {
        switch(model->focus_zone)
        {
            case FOCUS_BTN_MODIFY:  on_modify(); break;
            case FOCUS_BTN_SAVE:    on_save();   break;
            case FOCUS_BTN_LOAD:    on_load();   break;
            case FOCUS_BTN_CLOSE:   on_close();  break;
        }
        return 0;
    }

    return 0;
}

/* ── build dialog ─────────────────────────────────────────── */

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
    vk_window_set_title(dialog_window, " Settings ");
    vk_window_set_border_style(dialog_window, VK_FRAME_SINGLE);
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

    settings_listbox = vk_listbox_create(INTERIOR_WIDTH - 2, lb_height);
    vk_listbox_set_wrap(settings_listbox, FALSE);
    vk_listbox_set_highlight(settings_listbox, COLOR_WHITE, COLOR_RED);
    vk_widget_set_colors(VK_WIDGET(settings_listbox),
        COLOR_WHITE, COLOR_BLACK);

    listbox_frame = vk_frame_create(INTERIOR_WIDTH, lb_height + 2);
    vk_frame_set_border_style(listbox_frame, VK_FRAME_SINGLE);
    vk_frame_set_border_colors(listbox_frame, COLOR_YELLOW, COLOR_CYAN);
    vk_frame_set_border_attrs(listbox_frame, A_BOLD);
    vk_frame_set_child(listbox_frame, VK_WIDGET(settings_listbox));
    vk_widget_set_expand(VK_WIDGET(listbox_frame));

    listbox_scroller = vk_scroller_create(VK_SCROLLBAR_VERTICAL);
    vk_scroller_set_border_style(listbox_scroller, VK_FRAME_SINGLE);
    vk_scroller_set_border_colors(listbox_scroller,
        COLOR_BLACK, COLOR_CYAN);
    vk_scroller_set_scroll_source(listbox_scroller,
        VK_WIDGET(settings_listbox));
    vk_scroller_set_scroll_info(listbox_scroller,
        listbox_scroll_info);
    vk_widget_attach_scroller(VK_WIDGET(settings_listbox),
        listbox_scroller);

    button_hbox = vk_box_create(INTERIOR_WIDTH, 3,
        VK_BOX_HORIZONTAL, 5);
    vk_box_set_homogeneous(button_hbox, false);
    vk_widget_set_colors(VK_WIDGET(button_hbox), COLOR_BLACK, COLOR_CYAN);

    buttons[BTN_MODIFY] = vk_button_create("Modify");
    buttons[BTN_SAVE] = vk_button_create("Save");
    buttons[BTN_LOAD] = vk_button_create("Load");
    buttons[BTN_CLOSE] = vk_button_create("Close");

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

    vk_box_set_widget(button_hbox, 0, VK_WIDGET(buttons[BTN_MODIFY]));
    vk_box_set_widget(button_hbox, 1, VK_WIDGET(button_spacer));
    vk_box_set_widget(button_hbox, 2, VK_WIDGET(buttons[BTN_SAVE]));
    vk_box_set_widget(button_hbox, 3, VK_WIDGET(buttons[BTN_LOAD]));
    vk_box_set_widget(button_hbox, 4, VK_WIDGET(buttons[BTN_CLOSE]));

    vk_box_set_widget(main_vbox, 0, VK_WIDGET(listbox_frame));
    vk_box_set_widget(main_vbox, 1, VK_WIDGET(button_hbox));

    vk_window_set_child(dialog_window, VK_WIDGET(main_vbox));
    vk_object_set_kmio(VK_OBJECT(dialog_window), manage_settings_kmio);

    rebuild_listbox();

    model->selected = 0;
    vk_listbox_set_curr(settings_listbox, 0);
    vk_listbox_update(settings_listbox);

    update_button_highlights();

    pos_x = (scr_width - DIALOG_WIDTH) / 2;
    pos_y = (scr_height - DIALOG_HEIGHT) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vwm->manage_settings_popup = dialog_window;

    refresh_dialog();
}

/* ── public API ───────────────────────────────────────────── */

int
vwm_manage_settings_open(vk_widget_t *widget, void *anything)
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
                "Terminal too small for Settings");
            return -1;
        }
    }

    pending_desktops = 0;

    model = (settings_model_t *)calloc(1, sizeof(settings_model_t));
    model->focus_zone = FOCUS_LIST;

    rc_file = vwm_profile_rc_file_get(vwm);
    if(rc_file != NULL)
        strncpy(model->file_path, rc_file, PATH_MAX - 1);

    populate_app_options();
    model_load_from_vwm(vwm);

    build_dialog();

    vwm_panel_set_status(MANAGE_SETTINGS_HELP);

    return 0;
}

void
vwm_manage_settings_close(void)
{
    vwm_t *vwm;

    if(dialog_window == NULL) return;

    if(confirm_popup != NULL)
        confirm_popup_close();

    if(save_confirm_popup != NULL)
        save_confirm_popup_close();

    if(saved_popup != NULL)
        saved_popup_close();

    if(error_popup != NULL)
        error_popup_close();

    if(warning_popup != NULL)
        warning_popup_close();

    if(modify_popup != NULL)
        modify_popup_close();

    if(load_popup != NULL)
        load_popup_close();

    vwm = vwm_get_instance();

    vk_screen_detach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(dialog_window));

    vk_window_destroy(dialog_window);
    dialog_window = NULL;
    vwm->manage_settings_popup = NULL;

    main_vbox = NULL;
    button_hbox = NULL;
    listbox_frame = NULL;
    listbox_scroller = NULL;
    settings_listbox = NULL;
    memset(buttons, 0, sizeof(buttons));

    free(model);
    model = NULL;

    /*
        Apply a deferred desktop-count change now that the dialog is
        detached and destroyed, so no Settings widget is attached to a
        surface that may be removed.
    */
    if(pending_desktops >= 2)
    {
        vwm_apply_surface_count(pending_desktops);
        pending_desktops = 0;
    }

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
vwm_manage_settings_is_open(void)
{
    return (dialog_window != NULL);
}

vk_widget_t *
vwm_manage_settings_get_warning_popup(void)
{
    return VK_WIDGET(warning_popup);
}

vk_widget_t *
vwm_manage_settings_get_saved_popup(void)
{
    return VK_WIDGET(saved_popup);
}

vk_widget_t *
vwm_manage_settings_get_confirm_popup(void)
{
    return VK_WIDGET(confirm_popup);
}

vk_widget_t *
vwm_manage_settings_get_save_confirm_popup(void)
{
    return VK_WIDGET(save_confirm_popup);
}

vk_widget_t *
vwm_manage_settings_get_load_popup(void)
{
    return VK_WIDGET(load_popup);
}

vk_widget_t *
vwm_manage_settings_get_modify_popup(void)
{
    return VK_WIDGET(modify_popup);
}

void
vwm_manage_settings_handle_resize(void)
{
    vwm_t   *vwm;
    int     scr_w, scr_h;
    int     pos_x, pos_y;

    if(dialog_window == NULL) return;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    if(scr_w < DIALOG_WIDTH || scr_h < DIALOG_HEIGHT)
    {
        vwm_manage_settings_close();
        return;
    }

    pos_x = (scr_w - DIALOG_WIDTH) / 2;
    pos_y = (scr_h - DIALOG_HEIGHT) / 2;

    if(modify_popup != NULL)
        modify_popup_close();

    if(confirm_popup != NULL)
        confirm_popup_close();

    if(save_confirm_popup != NULL)
        save_confirm_popup_close();

    if(saved_popup != NULL)
        saved_popup_close();

    if(error_popup != NULL)
        error_popup_close();

    vk_widget_recreate(VK_WIDGET(dialog_window));
    vk_widget_move(VK_WIDGET(dialog_window), pos_x, pos_y);

    if(warning_popup != NULL)
        warning_popup_close();

    warning_popup_show();

    refresh_dialog();
}

int
vwm_manage_settings_mouse(MEVENT *mouse_event)
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
                    vwm_manage_settings_close();
                }
                else
                {
                    confirm_popup_close();
                }
            }
        }

        return 0;
    }

    if(save_confirm_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            int sp_x, sp_y, sp_w, sp_h;
            int sx, sy;

            vk_widget_get_position(VK_WIDGET(save_confirm_popup),
                &sp_x, &sp_y);
            vk_widget_get_metrics(VK_WIDGET(save_confirm_popup),
                &sp_w, &sp_h);

            sx = mouse_event->x - sp_x - 1;
            sy = mouse_event->y - sp_y - 1;

            if(sy >= sp_h - 4 && sy < sp_h - 2
                && sx >= 0 && sx < sp_w - 2)
            {
                int mid = (sp_w - 2) / 2;
                if(sx < mid)
                {
                    save_confirm_popup_close();
                    do_save();
                }
                else
                {
                    save_confirm_popup_close();
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

    if(error_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            error_popup_close();

        return 0;
    }

    if(warning_popup != NULL)
    {
        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            warning_popup_close();

        return 0;
    }

    if(modify_popup != NULL)
    {
        int mp_x, mp_y, mp_w, mp_h;
        int mx_r, my_r;
        int client_h;

        vk_widget_get_position(VK_WIDGET(modify_popup),
            &mp_x, &mp_y);
        vk_widget_get_metrics(VK_WIDGET(modify_popup),
            &mp_w, &mp_h);

        mx_r = mouse_event->x - mp_x - 1;
        my_r = mouse_event->y - mp_y - 1;
        client_h = mp_h - 5;

        if(mx_r < 0 || mx_r >= mp_w - 2 ||
           my_r < 0 || my_r >= mp_h - 2)
        {
            return 0;
        }

        if(setting_defs[modify_setting_idx].type == SETTING_TYPE_DROPDOWN
            && modify_listbox != NULL)
        {
            if(bs & BUTTON4_PRESSED)
            {
                vk_listbox_set_prev(modify_listbox);
                vk_listbox_update(modify_listbox);
                refresh_modify_popup();
                return 0;
            }

            if(bs & BUTTON5_PRESSED)
            {
                vk_listbox_set_next(modify_listbox);
                vk_listbox_update(modify_listbox);
                refresh_modify_popup();
                return 0;
            }
        }

        if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        {
            if(my_r >= client_h)
            {
                int mid = (mp_w - 2) / 2;
                if(mx_r < mid)
                    modify_popup_apply();
                else
                    modify_popup_close();
            }
            else if(setting_defs[modify_setting_idx].type
                == SETTING_TYPE_DROPDOWN && modify_listbox != NULL)
            {
                int scroll = vk_listbox_get_scroll_pos(modify_listbox);
                int clicked = scroll + my_r;
                int count = vk_listbox_get_item_count(modify_listbox);

                if(clicked >= 0 && clicked < count)
                {
                    vk_listbox_set_curr(modify_listbox, clicked);
                    vk_listbox_update(modify_listbox);
                    refresh_modify_popup();
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

        vk_widget_get_position(VK_WIDGET(load_popup), &lp_x, &lp_y);
        vk_widget_get_metrics(VK_WIDGET(load_popup), &lp_w, &lp_h);

        lx = mouse_event->x - lp_x - 1;
        ly = mouse_event->y - lp_y - 1;
        interior_w = lp_w - 2;
        interior_h = lp_h - 2;

        if(lx < 0 || lx >= interior_w || ly < 0 || ly >= interior_h)
            return 0;

        if(!(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED |
            BUTTON4_PRESSED | BUTTON5_PRESSED)))
            return 0;

        {
            vk_listbox_t *file_list;

            file_list = vk_filedialog_get_file_list(load_filedialog);

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
                int clicked = scroll_pos + ly;
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

    vk_widget_get_position(VK_WIDGET(dialog_window), &dlg_x, &dlg_y);

    rx = mouse_event->x - dlg_x - 1;
    ry = mouse_event->y - dlg_y - 1;

    if(rx < 0 || rx >= INTERIOR_WIDTH || ry < 0 || ry >= INTERIOR_HEIGHT)
        return 0;

    {
        int lb_height = INTERIOR_HEIGHT - 3 - 2;
        int btn_row = lb_height + 2;

        if(ry >= 1 && ry <= lb_height)
        {
            int item = (ry - 1) + vk_listbox_get_scroll_pos(settings_listbox);

            if(item >= 0 && item < NUM_SETTINGS &&
               (bs & (BUTTON1_CLICKED | BUTTON1_PRESSED)))
            {
                struct timespec now;
                long elapsed_ms;
                bool is_dblclick = false;

                clock_gettime(CLOCK_MONOTONIC, &now);

                if(item == list_last_click_item)
                {
                    elapsed_ms =
                        (now.tv_sec - list_last_click_time.tv_sec) * 1000
                        + (now.tv_nsec - list_last_click_time.tv_nsec)
                            / 1000000;

                    if(elapsed_ms >= 0 && elapsed_ms < 400)
                        is_dblclick = true;
                }

                list_last_click_time = now;
                list_last_click_item = item;

                model->selected = item;
                model->focus_zone = FOCUS_LIST;
                vk_listbox_set_curr(settings_listbox, item);
                refresh_dialog();

                if(is_dblclick)
                    on_modify();
            }
        }
        else if(ry >= btn_row && ry < btn_row + 3)
        {
            if(bs & (BUTTON1_CLICKED | BUTTON1_PRESSED))
            {
                int zone = -1;

                if(rx <= 9)
                    zone = FOCUS_BTN_MODIFY;
                else if(rx >= 23 && rx <= 28)
                    zone = FOCUS_BTN_SAVE;
                else if(rx >= 29 && rx <= 34)
                    zone = FOCUS_BTN_LOAD;
                else if(rx >= 35)
                    zone = FOCUS_BTN_CLOSE;

                if(zone >= 0)
                {
                    model->focus_zone = zone;
                    update_button_highlights();
                    refresh_dialog();

                    switch(zone)
                    {
                        case FOCUS_BTN_MODIFY: on_modify(); break;
                        case FOCUS_BTN_SAVE:   on_save();   break;
                        case FOCUS_BTN_LOAD:   on_load();   break;
                        case FOCUS_BTN_CLOSE:  on_close();  break;
                    }
                }
            }
        }
    }

    return 0;
}
