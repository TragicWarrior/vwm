#ifndef _H_VWM_CONFIG_
#define _H_VWM_CONFIG_

#include "cJSON.h"

/*
    JSON configuration helpers.  The config lives at
    $HOME/.config/vwm/config.json with three top-level objects:
    "hotkeys" (flat name -> hex keycode string), "programs" (array of
    {requires,title,bin,type,[params],[hidden],[scrollback],[start_home]}),
    and "settings".
*/

/* parse a config file into a cJSON tree (caller cJSON_Delete()s it).
   returns NULL when the file is missing or not valid JSON. */
cJSON   *vwm_config_load(const char *path);

/* serialize a cJSON tree to a file, pretty-printed.  returns 0 on success. */
int      vwm_config_store(const char *path, const cJSON *root);

/* build a fresh default config tree (sane hotkeys, the built-in VTerm
   emulation modes, and default settings). */
cJSON   *vwm_config_defaults(void);

/* typed object-member accessors that fall back to a default. */
const char  *vwm_json_str(const cJSON *obj, const char *key, const char *dflt);
int          vwm_json_int(const cJSON *obj, const char *key, int dflt);
int          vwm_json_bool(const cJSON *obj, const char *key, int dflt);

#endif
