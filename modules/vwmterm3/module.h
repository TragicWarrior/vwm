#ifndef _VWMTERM_MODULE_H_
#define _VWMTERM_MODULE_H_

#include <stdarg.h>
#include <stdbool.h>

#include "../../vwm.h"
#include "../../modules.h"

// extend the standard vwm module
struct _vwmterm_mod_s
{
    vwm_module_t    module;

    bool            fullscreen;

    uint32_t        flags;
    char            *bin_path;
    char            **exec_args;
};

typedef struct _vwmterm_mod_s   vwmterm_mod_t;

#define VWMTERM_MOD(x)          ((vwmterm_mod_t *)x)

vwm_module_t*   vwmterm_module_clone(vwm_module_t *mod);
int             vwmterm_module_configure(vwm_module_t *mod, va_list *argp);

#endif
