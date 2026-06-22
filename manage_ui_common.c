#include <ncursesw/curses.h>
#include <string.h>

#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "manage_ui_common.h"

void
vwm_listbox_scroll_info(vk_widget_t *child,
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

vk_popup_t *
vwm_warning_popup_show(void)
{
    vwm_t       *vwm;
    vk_box_t    *client;
    vk_popup_t  *popup;
    int         scr_w, scr_h;
    int         popup_w = 52;
    int         popup_h = 9;
    int         pos_x, pos_y;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    popup = vk_popup_create(popup_w, popup_h,
        VK_BORDER_SINGLE, "OK", NULL);
    vk_popup_set_title(popup, " Warning ");
    vk_popup_set_border_colors(popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(popup, A_NORMAL);
    {
        vk_box_t *bar = vk_popup_get_button_bar(popup);
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

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_popup_set_colors(popup, COLOR_RED, COLOR_WHITE);

    {
        vk_button_t *ok_btn = vk_popup_get_button(popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(popup);
    vk_screen_refresh(vwm->screen);

    return popup;
}

vk_popup_t *
vwm_saved_popup_show(const char *msg)
{
    vwm_t       *vwm;
    vk_label_t  *label;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 30;
    int         popup_h = 7;
    int         pos_x, pos_y;
    vk_popup_t  *popup;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    popup = vk_popup_create(popup_w, popup_h,
        VK_BORDER_SINGLE, "OK", NULL);
    vk_popup_set_title(popup, " Saved ");
    vk_popup_set_border_colors(popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(popup, A_BOLD);
    {
        vk_box_t *bar = vk_popup_get_button_bar(popup);
        if(bar != NULL)
        {
            vk_widget_set_colors(VK_WIDGET(bar), COLOR_WHITE, COLOR_BLUE);
            vk_widget_fill(VK_WIDGET(bar),
                ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
        }
    }

    client = vk_box_create(popup_w - 2, popup_h - 5,
        VK_BOX_VERTICAL, 1);
    vk_box_set_homogeneous(client, true);
    vk_widget_set_colors(VK_WIDGET(client), COLOR_WHITE, COLOR_BLUE);

    label = vk_label_create(popup_w - 2);
    vk_label_set_justify(label, VK_JUSTIFY_CENTER);
    vk_label_set_text(label, msg);
    vk_widget_set_colors(VK_WIDGET(label), COLOR_WHITE, COLOR_BLUE);
    vk_label_update(label);
    vk_box_set_widget(client, 0, VK_WIDGET(label));

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_popup_set_colors(popup, COLOR_WHITE, COLOR_BLUE);

    {
        vk_button_t *ok_btn = vk_popup_get_button(popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_BLUE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_WHITE, COLOR_BLUE)));
    vk_box_update(client);
    vk_popup_update(popup);
    vk_screen_refresh(vwm->screen);

    return popup;
}

vk_popup_t *
vwm_confirm_popup_show(void)
{
    vwm_t       *vwm;
    vk_box_t    *client;
    int         scr_w, scr_h;
    int         popup_w = 40;
    int         popup_h = 9;
    int         pos_x, pos_y;
    vk_popup_t  *popup;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    popup = vk_popup_create(popup_w, popup_h,
        VK_BORDER_SINGLE, "Discard", "Cancel", NULL);
    vk_popup_set_title(popup, " Confirm ");
    vk_popup_set_border_colors(popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(popup, A_NORMAL);
    vk_popup_set_colors(popup, COLOR_RED, COLOR_WHITE);
    {
        vk_box_t *bar = vk_popup_get_button_bar(popup);
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

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    {
        int count = vk_popup_get_button_count(popup);
        for(int i = 0; i < count; i++)
        {
            vk_button_t *btn = vk_popup_get_button(popup, i);

            if(i == 0)
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

    vk_widget_move(VK_WIDGET(popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(popup);
    vk_screen_refresh(vwm->screen);

    return popup;
}

vk_popup_t *
vwm_load_popup_show(const char *title, vk_filedialog_t **filedialog,
    const char *cur_path)
{
    vwm_t           *vwm;
    vk_popup_t      *popup;
    vk_filedialog_t *fd;
    int             scr_w, scr_h;
    int             interior_w, interior_h;
    int             pos_x, pos_y;
    const int       popup_w = 50;   /* matches each dialog's former LOAD_WIDTH  */
    const int       popup_h = 20;   /* matches each dialog's former LOAD_HEIGHT */

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    popup = vk_popup_create(popup_w, popup_h, VK_BORDER_SINGLE, NULL);
    vk_popup_set_title(popup, title);
    vk_popup_set_border_colors(popup, COLOR_WHITE, COLOR_BLUE);
    vk_popup_set_border_attrs(popup, A_BOLD);

    interior_w = popup_w - 2;
    interior_h = popup_h - 2;

    fd = vk_filedialog_create(interior_w, interior_h,
        VK_BORDER_SINGLE, false);
    vk_filedialog_set_colors(fd, COLOR_WHITE, COLOR_BLUE);
    /* grey active selection (matches the unfocused row), not red */
    vk_filedialog_set_highlight(fd, COLOR_BLACK, COLOR_WHITE);
    vk_listbox_set_unfocused(vk_filedialog_get_file_list(fd),
        COLOR_BLACK, COLOR_WHITE);
    vk_filedialog_set_button_colors(fd, COLOR_WHITE, COLOR_BLUE);
    vk_filedialog_set_button_attrs(fd, A_BOLD);

    {
        char dirpath[PATH_MAX];
        char *slash;

        strncpy(dirpath, cur_path, PATH_MAX - 1);
        dirpath[PATH_MAX - 1] = '\0';

        slash = strrchr(dirpath, '/');
        if(slash != NULL && slash != dirpath)
            *slash = '\0';
        else if(slash == dirpath)
            dirpath[1] = '\0';

        vk_filedialog_set_path(fd, dirpath);
    }

    vk_filedialog_update(fd);

    vk_popup_set_client(popup, VK_WIDGET(fd));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(fd));
        vk_widget_set_state(VK_WIDGET(fd), st & ~VK_STATE_EXPAND);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(popup));

    *filedialog = fd;
    return popup;
}

void
vwm_load_popup_paint_focus(vk_filedialog_t *filedialog, int focus)
{
    vk_widget_t *bar_w;
    vk_widget_t *ok = NULL;
    vk_widget_t *cancel = NULL;

    if(filedialog == NULL) return;

    /* the file browser is highlighted only while it holds focus */
    vk_listbox_set_focused(vk_filedialog_get_file_list(filedialog),
        focus == VWM_LOAD_FOCUS_FILEDIALOG);

    bar_w = vk_box_get_widget(VK_BOX(filedialog), 2);
    if(bar_w != NULL)
    {
        ok     = vk_box_get_widget(VK_BOX(bar_w), 0);
        cancel = vk_box_get_widget(VK_BOX(bar_w), 1);
    }

    if(ok != NULL)
    {
        vk_button_release(VK_BUTTON(ok));
        vk_widget_set_colors(ok,
            (focus == VWM_LOAD_FOCUS_OK) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(ok, A_BOLD);
        vk_button_update(VK_BUTTON(ok));
    }

    if(cancel != NULL)
    {
        vk_button_release(VK_BUTTON(cancel));
        vk_widget_set_colors(cancel,
            (focus == VWM_LOAD_FOCUS_CANCEL) ? COLOR_YELLOW : COLOR_WHITE,
            COLOR_BLUE);
        vk_widget_set_attrs(cancel, A_BOLD);
        vk_button_update(VK_BUTTON(cancel));
    }
}

vk_popup_t *
vwm_error_popup_show(const char *msg, int popup_w, int popup_h)
{
    vwm_t       *vwm;
    vk_box_t    *client;
    vk_label_t  *label;
    int         scr_w, scr_h;
    int         pos_x, pos_y;
    vk_popup_t  *popup;

    vwm = vwm_get_instance();
    getmaxyx(vk_screen_get_window(vwm->screen), scr_h, scr_w);

    popup = vk_popup_create(popup_w, popup_h,
        VK_BORDER_SINGLE, "OK", NULL);
    vk_popup_set_title(popup, " Error ");
    vk_popup_set_border_colors(popup, COLOR_RED, COLOR_WHITE);
    vk_popup_set_border_attrs(popup, A_NORMAL);
    {
        vk_box_t *bar = vk_popup_get_button_bar(popup);
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

    vk_popup_set_client(popup, VK_WIDGET(client));

    {
        uint32_t st = vk_widget_get_state(VK_WIDGET(client));
        vk_widget_set_state(VK_WIDGET(client), st & ~VK_STATE_EXPAND);
    }

    vk_popup_set_colors(popup, COLOR_RED, COLOR_WHITE);

    {
        vk_button_t *ok_btn = vk_popup_get_button(popup, 0);
        vk_widget_set_colors(VK_WIDGET(ok_btn), COLOR_YELLOW, COLOR_WHITE);
        vk_widget_set_attrs(VK_WIDGET(ok_btn), A_BOLD);
        vk_button_update(ok_btn);
    }

    pos_x = (scr_w - popup_w) / 2;
    pos_y = (scr_h - popup_h) / 2;
    if(pos_x < 0) pos_x = 0;
    if(pos_y < 0) pos_y = 0;

    vk_widget_move(VK_WIDGET(popup), pos_x, pos_y);

    vk_screen_attach_widget(vwm->screen,
        vk_screen_get_active_surface(vwm->screen),
        VK_WIDGET(popup));

    vk_widget_fill(VK_WIDGET(client),
        ' ' | COLOR_PAIR(vdk_color_pair(COLOR_RED, COLOR_WHITE)));
    vk_box_update(client);
    vk_popup_update(popup);
    vk_screen_refresh(vwm->screen);

    return popup;
}
