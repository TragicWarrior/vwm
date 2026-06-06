#ifndef _VWM_MANAGE_HOTKEYS_H_
#define _VWM_MANAGE_HOTKEYS_H_

#include <stdbool.h>

#include <vdk.h>

int     vwm_manage_hotkeys_open(vk_widget_t *widget, void *anything);
void    vwm_manage_hotkeys_close(void);
bool    vwm_manage_hotkeys_is_open(void);
int     vwm_manage_hotkeys_mouse(MEVENT *mouse_event);
void    vwm_manage_hotkeys_handle_resize(void);
vk_widget_t *vwm_manage_hotkeys_get_load_popup(void);
vk_widget_t *vwm_manage_hotkeys_get_confirm_popup(void);
vk_widget_t *vwm_manage_hotkeys_get_error_popup(void);
vk_widget_t *vwm_manage_hotkeys_get_warning_popup(void);

#endif
