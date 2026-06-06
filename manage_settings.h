#ifndef _VWM_MANAGE_SETTINGS_H_
#define _VWM_MANAGE_SETTINGS_H_

#include <stdbool.h>

#include <vdk.h>

int     vwm_manage_settings_open(vk_widget_t *widget, void *anything);
void    vwm_manage_settings_close(void);
bool    vwm_manage_settings_is_open(void);
int     vwm_manage_settings_mouse(MEVENT *mouse_event);
void    vwm_manage_settings_handle_resize(void);
vk_widget_t *vwm_manage_settings_get_warning_popup(void);
vk_widget_t *vwm_manage_settings_get_saved_popup(void);
vk_widget_t *vwm_manage_settings_get_confirm_popup(void);
vk_widget_t *vwm_manage_settings_get_save_confirm_popup(void);
vk_widget_t *vwm_manage_settings_get_load_popup(void);
vk_widget_t *vwm_manage_settings_get_modify_popup(void);

#endif
