#include <ncursesw/curses.h>

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
