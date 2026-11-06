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

/*
    Decode one programs[] object.  Pointers alias `entry`.  See
    programs.h for which keys come back NULL and which come back "".
*/
int
vwm_program_from_json(const cJSON *entry, vwm_program_fields_t *out)
{
    const char  *type;
    int         value;

    /* both callers skip a non-object */
    if(entry == NULL || out == NULL || !cJSON_IsObject(entry)) return -1;

    out->requires = vwm_json_str(entry, "requires", NULL);

    /* missing title or bin is an empty string in both readers */
    out->title = vwm_json_str(entry, "title", "");
    out->bin = vwm_json_str(entry, "bin", "");

    /* missing params stays NULL: the menu path then does not join a space */
    out->params = vwm_json_str(entry, "params", NULL);

    /* missing or unknown type is Misc.  "Misc" is the last type name,
       so a missing key and an explicit "Misc" land on the same value.
       An empty or unknown string is not in the type list and becomes
       Misc too. */
    type = vwm_json_str(entry, "type", "Misc");
    value = vwm_module_type_value((char *)type);
    if(value == -1) value = VWM_MOD_TYPE_MISC;
    out->type = value;

    out->hidden = (vwm_json_bool(entry, "hidden", 0) != 0);
    out->scrollback = vwm_json_int(entry, "scrollback", 0);
    out->start_home = (vwm_json_bool(entry, "start_home", 0) != 0);
    out->term_width = vwm_json_int(entry, "width", 80);
    out->term_height = vwm_json_int(entry, "height", 25);

    return 0;
}

/*
    Add the programs in a tree the caller parsed.  The caller frees
    `root`.  This does not purge what is already loaded.
*/
int
vwm_programs_load_from_json(vwm_t *vwm, const cJSON *root)
{
    cJSON                   *programs;
    cJSON                   *entry;
    vwm_module_t            *module;
    vwm_program_fields_t    fields;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    /* missing or unparsable file: add nothing */
    if(root == NULL) return -1;

    programs = cJSON_GetObjectItemCaseSensitive(root, "programs");

    /* not an array: add nothing.  the caller still frees `root`. */
    if(!cJSON_IsArray(programs)) return -1;

    cJSON_ArrayForEach(entry, programs)
    {
        const char  *title;
        const char  *bin;
        char        **args;
        char        buf[256];

        if(vwm_program_from_json(entry, &fields) != 0) continue;

        /* every program requires an installed terminal module.
           the Apps dialog keeps an entry that fails this; the menu does not. */
        if(fields.requires == NULL) continue;

        module = vwm_module_find_by_name((char *)fields.requires);
        if(module == NULL) continue;

        title = (fields.title != NULL) ? fields.title : "";
        bin = (fields.bin != NULL) ? fields.bin : "";

        module = vwm_module_clone(module);
        module->fd_argv = NULL;
        module->scrollback = fields.scrollback;
        module->start_home = fields.start_home;
        module->term_width = fields.term_width;
        module->term_height = fields.term_height;
        vwm_module_set_title(module, (char *)title);
        vwm_module_set_type(module, fields.type);

        /* copy bin and params into a buffer so we can explode it.
           the 256-byte join is this path only; the dialog keeps params whole. */
        if(fields.params != NULL)
        {
            snprintf(buf, sizeof(buf), "%s %s", bin, fields.params);
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

        vwm_module_set_hidden(module, fields.hidden);

        vwm_module_configure(module, (char *)bin, args);
        vwm_module_set_zone(module, MODULE_ZONE_USER);
        vwm_module_add(module);
        strfreev(args);
    }

    return 0;
}

/*
    Read the rc file and add its programs.  Does not purge first;
    vwm_programs_reload does that.
*/
int
vwm_programs_load(vwm_t *vwm)
{
    cJSON   *root;
    int     status;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    root = vwm_config_load(vwm->profile->rc_file);
    status = vwm_programs_load_from_json(vwm, root);
    if(root != NULL) cJSON_Delete(root);

    return status;
}


