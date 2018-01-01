#include <stdarg.h>
#include <string.h>
#include <inttypes.h>

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

    // clone just the standard module stuff
    memcpy(vwmterm_mod, mod, sizeof(vwm_module_t));

    vwmterm_mod->fullscreen = VWMTERM_MOD(mod)->fullscreen;

    return VWM_MODULE(vwmterm_mod);
}

int
vwmterm_module_configure(vwm_module_t *mod, va_list *argp)
{
    vwmterm_mod_t   *vwmterm_mod;

    if(mod == NULL) return -1;
    if(argp == NULL) return -1;

    vwmterm_mod = (vwmterm_mod_t *)mod;

    vwmterm_mod->flags = va_arg(*argp, uint32_t);
    vwmterm_mod->bin_path = strdup(va_arg(*argp, char *));
    vwmterm_mod->exec_args = strdupv(va_arg(*argp, char **), 10);

    return 0;
}
