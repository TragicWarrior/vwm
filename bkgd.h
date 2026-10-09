#ifndef _H_VWM_BKGD_
#define _H_VWM_BKGD_

#include <vdk.h>

void    vwm_bkgd_simple_normal(vk_screen_t *screen, int surface_id,
            WINDOW *canvas);
void    vwm_bkgd_simple_winman(vk_screen_t *screen, int surface_id,
            WINDOW *canvas);

void    vwm_apply_desktop_bkgd(int surface_id);
void    vwm_apply_desktop_bkgd_all(void);

void    vwm_invalidate_wallpaper_cache(int surface_id);
void    vwm_invalidate_wallpaper_cache_all(void);
void    vwm_invalidate_wallpaper_cache_all_orphan(void);
/* forget the cached big-font host name without freeing it; for when the
   screen has been rebuilt on another terminal (see bkgd.c) */
void    vwm_hostname_cache_orphan(void);

bool    vwm_has_utf8(void);

#endif
