#ifndef _VWM_MODULES_H_
#define _VWM_MODULES_H_

#include <limits.h>

#include <viper.h>

// ensure that x-macro for modules.def is not already defined
#ifdef  X_MOD
#undef  X_MOD
#endif

#define X_MOD(modtype_val,modtype_text) modtype_val,
enum
{
#include "modules.def"
VWM_MOD_TYPE_MAX
};
#undef  X_MOD


#define X_MOD(modtype_val,modtype_text) modtype_text,
    static char *modtype_desc[] = {
#include "modules.def"
NULL
};
#undef  X_MOD


#include "list.h"

struct _vwm_module_s
{
    char                modpath[NAME_MAX];
    void                *handle;

    int                 type;
    char                title[NAME_MAX];

    struct list_head    list;

    vwnd_t*             (*main)             (const char *);
    void                *anything;
};

int vwm_menu_helper(vk_widget_t *widget, void *anything);

#define VWM_MODULE(x)   ((vwm_module_t *)x)


#endif



