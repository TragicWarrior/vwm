#ifndef _VWM_MODULES_H_
#define _VWM_MODULES_H_

#include <stdarg.h>
#include <limits.h>

#include <viper.h>

// ensure that x-macro for modules.def is not already defined
#ifdef  X_MOD
#undef  X_MOD
#endif

#define X_MOD(modtype_val, modtype_text) modtype_val,
enum
{
#include "modules.def"
VWM_MOD_TYPE_MAX
};
#undef  X_MOD

enum
{
    MODULE_ZONE_CORE    = 0x00,
    MODULE_ZONE_APP,
    MODULE_ZONE_USER,
};

#include "vwm.h"
#include "list.h"

struct _vwm_module_s
{
    char                    modpath[NAME_MAX];

    char                    name[NAME_MAX];     // a unique name for the module
    char                    title[NAME_MAX];    // module "display name"
    int                     type;               // classificaton of module

    int                     zone;               // built-in, or user loaded

    vwm_module_t*           (*clone)            (vwm_module_t *);
    int                     (*configure)        (vwm_module_t *, va_list *);

    struct list_head        list;

    vwnd_t*                 (*main)             (vwm_module_t *);
    void                    *anything;
};

// this is the standard callback which clones a module
vwm_module_t*   vwm_module_simple_clone(vwm_module_t *mod);
int             vwm_module_set_zone(vwm_module_t *mod, int zone);

int vwm_menu_helper(vk_widget_t *widget, void *anything);

#define VWM_MODULE(x)   ((vwm_module_t *)x)


#endif



