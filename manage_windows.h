#ifndef _VWM_MANAGE_WINDOWS_H_
#define _VWM_MANAGE_WINDOWS_H_

#include <stdbool.h>

#include <vdk.h>

int             vwm_manage_windows_open(vk_widget_t *widget, void *anything);
void            vwm_manage_windows_close(void);
bool            vwm_manage_windows_is_open(void);
int             vwm_manage_windows_mouse(MEVENT *mouse_event);
void            vwm_manage_windows_handle_resize(void);
vk_widget_t*    vwm_manage_windows_get_warning_popup(void);

#endif
