#include <viper.h>

#include "events.h"

int
vwm_main_menu_ON_TERM_RESIZED(vwnd_t *vwnd, void *arg)
{
    vk_menu_t   *menu;
    int         width;
    int         height;
    int         scr_width;
    int         scr_height;

    if(vwnd == NULL) return -1;
    if(arg == NULL) return -1;

    menu = (vk_menu_t *)arg;

    vk_widget_get_metrics(VK_WIDGET(menu), &width, &height);

    getmaxyx(CURRENT_SCREEN, scr_height, scr_width);
    scr_height -= 4;
    scr_width -= 4;

    if(width < scr_width && height < scr_height) return 0;

    if(height > scr_height) height = scr_height;
    if(width > scr_width) width = scr_width;

    vk_widget_resize(VK_WIDGET(menu), width, height);
    viper_wresize_abs(vwnd, width + 2, height + 2);

    vk_menu_update(menu);
    vk_widget_draw(VK_WIDGET(menu));

    viper_screen_redraw(CURRENT_SCREEN_ID, REDRAW_ALL | REDRAW_BACKGROUND);

    return 0;
}

