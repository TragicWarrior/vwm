#ifndef _VWM_MANAGE_SETTINGS_H_
#define _VWM_MANAGE_SETTINGS_H_

#include <stdbool.h>

#include <vdk.h>

int     vwm_manage_settings_open(vk_widget_t *widget, void *anything);
void    vwm_manage_settings_close(void);
bool    vwm_manage_settings_is_open(void);
int     vwm_manage_settings_mouse(MEVENT *mouse_event);
/* the popup or dialog of this tool under screen cell (mx, my), or NULL */
vk_widget_t *vwm_manage_settings_hit_test(int mx, int my);
void    vwm_manage_settings_handle_resize(void);

#endif
