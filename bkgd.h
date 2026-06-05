#ifndef _H_VWM_BKGD_
#define _H_VWM_BKGD_

#include <vdk.h>

void    vwm_bkgd_simple_normal(vk_screen_t *screen, int surface_id,
            WINDOW *canvas);
void    vwm_bkgd_simple_winman(vk_screen_t *screen, int surface_id,
            WINDOW *canvas);

void    vwm_apply_desktop_bkgd(int surface_id);
void    vwm_apply_desktop_bkgd_all(void);

#endif
