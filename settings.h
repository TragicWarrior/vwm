#ifndef _VWM_SETTINGS_H_
#define _VWM_SETTINGS_H_

#include <stddef.h>

#include "cJSON.h"
#include "vwm.h"

/*
    One row per hotkey: its name in the config file, where the live
    value sits in vwm_t, and the default used when nothing has set it.
    The hotkeys dialog keeps its own labels beside this, in the same
    order.  A key the file does not mention keeps its current value.
*/
typedef struct
{
    const char  *name;
    size_t      offset;
    int32_t     default_value;
}
vwm_hotkey_def_t;

#define VWM_HOTKEY_COUNT    14

extern const vwm_hotkey_def_t vwm_hotkeys[VWM_HOTKEY_COUNT];

/* the live value of hotkey `index`, inside `vwm` */
int32_t *vwm_hotkey_field(vwm_t *vwm, int index);

int     vwm_settings_save(vwm_t *vwm);
int     vwm_settings_load_hotkeys(vwm_t *vwm);

/*
    Apply hotkeys from a tree the caller already parsed.  The caller
    frees `root`.  A NULL root keeps the current values, the same as a
    missing file.  Returns -1 when vwm or its rc path is missing.
*/
int     vwm_settings_load_hotkeys_from_json(vwm_t *vwm, const cJSON *root);

/*
    Apply the "settings" object of a tree the caller already parsed.
    The caller frees `root`.  A NULL root, or a missing vwm or rc
    path, changes nothing.
*/
void    vwm_settings_load_general_from_json(vwm_t *vwm, const cJSON *root);

void    vwm_settings_save_general(vwm_t *vwm);

// gint  vwm_settings_bkgd_load(gchar *line_data);
// gint  vwm_settings_scrsaver_load(gchar *line_data);

#endif
