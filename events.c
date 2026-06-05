#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "mainmenu.h"
#include "events.h"

void
vwm_dropdown_ON_TERM_RESIZED(void)
{
    vwm_t           *vwm;
    vk_window_t     *menu;
    vk_listbox_t    *listbox;
    int             content_width;
    int             content_height;
    int             width;
    int             height;
    int             scr_width;
    int             scr_height;

    vwm = vwm_get_instance();
    menu = vwm->menu;

    if(menu == NULL) return;

    listbox = VK_LISTBOX(vk_window_get_child(menu));
    vk_listbox_get_metrics(listbox, &content_width, &content_height);

    getmaxyx(vk_screen_get_window(vwm->screen), scr_height, scr_width);
    scr_width -= 4;
    scr_height = (scr_height * 3) / 4;

    width = content_width;
    height = content_height;
    if(width > scr_width) width = scr_width;
    if(height > scr_height) height = scr_height;

    vk_widget_resize(VK_WIDGET(listbox), width, height);
    vk_widget_resize(VK_WIDGET(menu), width + 2, height + 2);

    vk_listbox_update(listbox);
    vk_window_update(menu);

    vk_screen_refresh(vwm->screen);
}
