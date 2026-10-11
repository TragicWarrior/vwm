#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "mainmenu.h"
#include "events.h"

void
vwm_dropdown_ON_TERM_RESIZED(void)
{
    vwm_t           *vwm;

    vwm = vwm_get_instance();

    if(vwm->menu == NULL) return;

    /* the dropdown at the size it would be built with now */
    vwm_menubar_refit_dropdown();

    /* the Apps submenu is placed against the dropdown: rebuild it there */
    vwm_menubar_refresh_submenu();

    vk_screen_refresh(vwm->screen);
}
