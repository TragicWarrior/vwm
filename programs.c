#include <stdlib.h>
#include <libconfig.h>

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
        free(mod);
    }
}

int
vwm_programs_reload(void)
{
    vwm_t   *vwm;

    vwm = vwm_get_instance();

    vwm_programs_purge(vwm);
    config_destroy(&vwm->config);

    return vwm_programs_load(vwm);
}

int
vwm_programs_load(vwm_t *vwm)
{
    config_setting_t    *programs;
    config_setting_t    *entry;
    vwm_module_t        *module;

    const char          *requires;
    const char          *title;
    const char          *type;
    const char          *bin;
    const char          *params;
    char                **args;
    int                 value;

    char                buf[256];
    int                 i = 0;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    config_init(&vwm->config);
    config_read_file(&vwm->config, vwm->profile->rc_file);

    programs = config_lookup(&vwm->config, "programs");

    if(programs == NULL)
    {
        config_destroy(&vwm->config);
        return -1;
    }

    do
    {
        module = NULL;
        entry = config_setting_get_elem(programs, i);

        if(entry != NULL)
        {
            // check to see if program has a dependency
            requires = NULL;
            params = NULL;
            config_setting_lookup_string(entry, "requires", &requires);

            // if the program requires a module that's not installed, move on
            if(requires != NULL)
            {
                module = vwm_module_find_by_name((char *)requires);

                if(module == NULL)
                {
                    i++;
                    continue;
                }
            }

            config_setting_lookup_string(entry, "title", &title);
            config_setting_lookup_string(entry, "type", &type);
            config_setting_lookup_string(entry, "bin", &bin);
            config_setting_lookup_string(entry, "params", &params);

            value = vwm_module_type_value((char *)type);
            if(value == -1) value = VWM_MOD_TYPE_MISC;

            module = vwm_module_clone(module);
            vwm_module_set_title(module, (char *)title);
            vwm_module_set_type(module, value);

            // copy it bin and params  into a buffer so we can explode it
            if(params != NULL)
            {
                snprintf(buf, sizeof(buf), "%s %s", bin, params);
                args = strsplitv(buf, " ");
            }
            else
            {
                args = strcatv(NULL, (char *)bin);
            }

            {
                int hidden_val = 0;
                config_setting_lookup_bool(entry, "hidden", &hidden_val);
                vwm_module_set_hidden(module, hidden_val != 0);
            }

            vwm_module_configure(module, bin, args);
            vwm_module_set_zone(module, MODULE_ZONE_USER);
            vwm_module_add(module);
            strfreev(args);
            args = NULL;
        }
        i++;
    }
    while(entry != NULL);

    return 0;
}


