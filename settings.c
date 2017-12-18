#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libconfig.h>

#include "vwm.h"
#include "settings.h"
#include "profile.h"
#include "private.h"
#include "hotkeys.h"

int
vwm_settings_load(vwm_t *vwm)
{
    char    *value;
    int32_t keystroke;
    int     retval;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    config_init(&vwm->config);
    config_read_file(&vwm->config, vwm->profile->rc_file);

    retval = config_lookup_string(&vwm->config, "hotkeys.menu", &value);

    if(retval == CONFIG_TRUE)
    {
        retval = sscanf(value, "%x", &keystroke);
        vwm->hotkey_menu = keystroke;
    }

    config_destroy(&vwm->config);

    return 0;
}


