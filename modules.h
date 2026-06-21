#ifndef _VWM_MODULES_H_
#define _VWM_MODULES_H_

#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>

#include <vdk.h>

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
    bool                    hidden;

    vwm_module_t*           (*clone)            (vwm_module_t *);
    int                     (*configure)        (vwm_module_t *, va_list *);

    struct list_head        list;

    vk_window_t*            (*main)             (vwm_module_t *);
    void                    *anything;

    /* For a USER program whose params hold the "%fd" launch token: the
       raw argv (bin + params, %fd unsubstituted), strdupv'd in
       programs.c; NULL otherwise.  The launch picker substitutes the
       chosen file into a copy of this at run time. */
    char                    **fd_argv;

    /* Per-app vterm scrollback override, in lines (Manage Apps).  0 means
       inherit the vterm default (4x the terminal height); a positive
       value is applied via vterm_set_history_size at launch. */
    int                     scrollback;
};

// this is the standard callback which clones a module
vwm_module_t*   vwm_module_simple_clone(vwm_module_t *mod);
int             vwm_module_set_zone(vwm_module_t *mod, int zone);
void            vwm_module_set_hidden(vwm_module_t *mod, bool hidden);
bool            vwm_module_is_hidden(vwm_module_t *mod);

int vwm_menu_helper(vk_widget_t *widget, void *anything);

#define VWM_MODULE(x)   ((vwm_module_t *)x)


#endif



