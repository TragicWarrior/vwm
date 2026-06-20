#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "vwm.h"
#include "modules.h"
#include "profile.h"
#include "programs.h"
#include "private.h"
#include "strings.h"

static void
vwm_programs_purge(vwm_t *vwm)
{
    struct list_head    *pos;
    struct list_head    *tmp;
    vwm_module_t        *mod;

    list_for_each_safe(pos, tmp, &vwm->module_list)
    {
        mod = list_entry(pos, vwm_module_t, list);

        if(vwm_module_get_zone(mod) != MODULE_ZONE_USER) continue;

        list_del(pos);
        if(mod->fd_argv != NULL) strfreev(mod->fd_argv);
        free(mod);
    }
}

int
vwm_programs_reload(void)
{
    vwm_t   *vwm;

    vwm = vwm_get_instance();

    vwm_programs_purge(vwm);

    return vwm_programs_load(vwm);
}

int
vwm_programs_load(vwm_t *vwm)
{
    cJSON               *root;
    cJSON               *programs;
    cJSON               *entry;
    vwm_module_t        *module;

    const char          *requires;
    const char          *title;
    const char          *type;
    const char          *bin;
    const char          *params;
    char                **args;
    int                 value;

    char                buf[256];

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    root = vwm_config_load(vwm->profile->rc_file);
    if(root == NULL) return -1;

    programs = cJSON_GetObjectItemCaseSensitive(root, "programs");

    if(!cJSON_IsArray(programs))
    {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(entry, programs)
    {
        if(!cJSON_IsObject(entry)) continue;

        /* every program requires an installed terminal module */
        requires = vwm_json_str(entry, "requires", NULL);
        if(requires == NULL) continue;

        module = vwm_module_find_by_name((char *)requires);
        if(module == NULL) continue;

        title  = vwm_json_str(entry, "title", "");
        type   = vwm_json_str(entry, "type", "Misc");
        bin    = vwm_json_str(entry, "bin", "");
        params = vwm_json_str(entry, "params", NULL);

        value = vwm_module_type_value((char *)type);
        if(value == -1) value = VWM_MOD_TYPE_MISC;

        module = vwm_module_clone(module);
        module->fd_argv = NULL;
        vwm_module_set_title(module, (char *)title);
        vwm_module_set_type(module, value);

        // copy bin and params into a buffer so we can explode it
        if(params != NULL)
        {
            snprintf(buf, sizeof(buf), "%s %s", bin, params);
            args = strsplitv(buf, " ");
        }
        else
        {
            args = strcatv(NULL, (char *)bin);
        }

        /* if any arg carries the "%fd" launch token, stash the raw argv so
           the launch picker can substitute the chosen file at run time */
        {
            int i;
            for(i = 0; args[i] != NULL; i++)
            {
                if(strstr(args[i], "%fd") != NULL)
                {
                    module->fd_argv = strdupv(args, 0);
                    break;
                }
            }
        }

        vwm_module_set_hidden(module, vwm_json_bool(entry, "hidden", 0) != 0);

        vwm_module_configure(module, (char *)bin, args);
        vwm_module_set_zone(module, MODULE_ZONE_USER);
        vwm_module_add(module);
        strfreev(args);
    }

    cJSON_Delete(root);

    return 0;
}


