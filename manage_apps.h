#ifndef _VWM_MANAGE_APPS_H_
#define _VWM_MANAGE_APPS_H_

#include <stdbool.h>

#include <vdk.h>

int     vwm_manage_apps_open(vk_widget_t *widget, void *anything);
void    vwm_manage_apps_close(void);
bool    vwm_manage_apps_is_open(void);
int     vwm_manage_apps_mouse(MEVENT *mouse_event);
/* the popup or dialog of this tool under screen cell (mx, my), or NULL */
vk_widget_t *vwm_manage_apps_hit_test(int mx, int my);
void    vwm_manage_apps_handle_resize(void);

#endif
