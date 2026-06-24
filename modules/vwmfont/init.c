/*
    vwmfont module entry.  On load it registers the big-font renderer with
    vwm core, so the bottom-left hostname label can render through it.  The
    actual parser/renderer lives in vwmfont.c, kept free of vwm-core deps
    so it stays independently unit-testable.
*/

#include "vwmfont.h"
#include "../../vwm.h"

/* adapt the typed public API to the core's int-based hook signature */
static vk_widget_t *
vwmfont_render_hook(const char *text, int size, int fill)
{
    return vwmfont_render(text, (vwmfont_size_t)size, (vwmfont_fill_t)fill);
}

int
vwm_mod_init(const char *modpath)
{
    (void)modpath;

    vwmfont_init(NULL);
    vwm_register_font_renderer(vwmfont_render_hook,
        vwmfont_apply_colors, vwmfont_free_widget);

    return 0;
}
