#include <viper.h>

#include "events.h"

int
vwm_main_menu_ON_TERM_RESIZED(vwnd_t *vwnd, void *arg)
{
    vk_frame_t      *frame;
    vk_listbox_t    *listbox;
    int             content_width;
    int             content_height;
    int             width;
    int             height;
    int             scr_width;
    int             scr_height;

    if(vwnd == NULL) return -1;
    if(arg == NULL) return -1;

    frame = (vk_frame_t *)arg;
    listbox = VK_LISTBOX(vk_frame_get_child(frame));

    vk_listbox_get_metrics(listbox, &content_width, &content_height);

    getmaxyx(CURRENT_SCREEN, scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    width = content_width;
    height = content_height;
    if(width > scr_width) width = scr_width;
    if(height > scr_height) height = scr_height;

    vk_widget_resize(VK_WIDGET(frame), width + 2, height + 2);
    viper_wresize_abs(vwnd, width + 2, height + 2);

    vk_listbox_update(listbox);
    vk_frame_update(frame);
    vk_widget_draw(VK_WIDGET(frame));

    viper_screen_redraw(CURRENT_SCREEN_ID, REDRAW_ALL | REDRAW_BACKGROUND);

    return 0;
}

