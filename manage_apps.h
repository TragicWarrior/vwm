#ifndef _VWM_MANAGE_APPS_H_
#define _VWM_MANAGE_APPS_H_

#include <stdbool.h>

#include <vdk.h>

int     vwm_manage_apps_open(vk_widget_t *widget, void *anything);
void    vwm_manage_apps_close(void);
bool    vwm_manage_apps_is_open(void);
int     vwm_manage_apps_mouse(MEVENT *mouse_event);
void    vwm_manage_apps_handle_resize(void);
vk_widget_t *vwm_manage_apps_get_warning_popup(void);
vk_widget_t *vwm_manage_apps_get_edit_popup(void);
vk_widget_t *vwm_manage_apps_get_dropdown_popup(void);
vk_widget_t *vwm_manage_apps_get_load_popup(void);

#endif
