#include <ncursesw/curses.h>

#include <vdk.h>

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
