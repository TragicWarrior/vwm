#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <vterm.h>

#include "module.h"

#include "../../vwm.h"
#include "../../modules.h"
#include "../../strings.h"

vwm_module_t*
vwmterm_module_clone(vwm_module_t *mod)
{
    vwmterm_mod_t   *vwmterm_mod;

    if(mod == NULL) return NULL;

    vwmterm_mod = (vwmterm_mod_t *)calloc(1, sizeof(vwmterm_mod_t));

    // memcpy the entire module
    memcpy(vwmterm_mod, mod, sizeof(vwmterm_mod_t));

    return VWM_MODULE(vwmterm_mod);
}

int
vwmterm_module_configure(vwm_module_t *mod, va_list *argp)
{
    vwmterm_mod_t   *vwmterm_mod;

    if(mod == NULL) return -1;
    if(argp == NULL) return -1;

    vwmterm_mod = (vwmterm_mod_t *)mod;

    /* free any previous config so re-configuring (e.g. a %fd launch that
       substitutes a fresh file each time) doesn't leak the old strings;
       calloc'd on create, so these are NULL on the first call */
    free(vwmterm_mod->bin_path);
    strfreev(vwmterm_mod->exec_args);

    vwmterm_mod->bin_path = strdup(va_arg(*argp, char *));
    vwmterm_mod->exec_args = strdupv(va_arg(*argp, char **), 0);

    return 0;
}
