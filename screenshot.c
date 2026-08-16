#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <vdk.h>

#include "vwm.h"
#include "private.h"
#include "screenshot.h"
#include "screenshot_priv.h"

#define SCRSHOT_PX      16

#ifndef VWM_FONTDIR
#define VWM_FONTDIR     "/usr/local/share/vwm/fonts"
#endif

#ifndef VWM_FONTS_SRCDIR
#define VWM_FONTS_SRCDIR    "fonts"
#endif

static const char *regular_candidates[] =
{
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/local/share/fonts/dejavu/DejaVuSansMono.ttf",
    VWM_FONTDIR "/DejaVuSansMono.ttf",
    VWM_FONTS_SRCDIR "/DejaVuSansMono.ttf",
    NULL
};

static const char *bold_candidates[] =
{
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/local/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
    VWM_FONTDIR "/DejaVuSansMono-Bold.ttf",
    VWM_FONTS_SRCDIR "/DejaVuSansMono-Bold.ttf",
    NULL
};

static int
font_readable(const char *path)
{
    if(path == NULL || path[0] == '\0')
        return 0;

    return (access(path, R_OK) == 0);
}

/*
    If override is non-empty it is the only path considered: missing
    file is a hard error (do not fall through).  Otherwise walk
    candidates and take the first readable path.
*/
static int
resolve_font(const char *override, const char *const *candidates,
    char *out, size_t out_sz)
{
    int i;

    if(override != NULL && override[0] != '\0')
    {
        if(!font_readable(override))
            return -1;

        snprintf(out, out_sz, "%s", override);
        return 0;
    }

    for(i = 0; candidates[i] != NULL; i++)
    {
        if(font_readable(candidates[i]))
        {
            snprintf(out, out_sz, "%s", candidates[i]);
            return 0;
        }
    }

    return -1;
}

int
vwm_screenshot_save(int target, const char *path)
{
    vwm_t           *vwm;
    WINDOW          *src = NULL;
    vk_widget_t     *capwin;
    scrshot_cfg_t   cfg;
    scrshot_grid_t  *grid = NULL;
    uint8_t         *rgb = NULL;
    int             img_w = 0;
    int             img_h = 0;
    int             code;
    char            regular[PATH_MAX];
    char            bold[PATH_MAX];
    const char      *ovr_reg = NULL;
    const char      *ovr_bold = NULL;

#ifdef VWM_SCREENSHOT_FONT
    ovr_reg = VWM_SCREENSHOT_FONT;
#endif
#ifdef VWM_SCREENSHOT_FONT_BOLD
    ovr_bold = VWM_SCREENSHOT_FONT_BOLD;
#endif

    if(path == NULL || path[0] == '\0')
        return VWM_SHOT_ERR_PNG;

    vwm = vwm_get_instance();
    if(vwm == NULL || vwm->screen == NULL)
        return VWM_SHOT_ERR_ALLOC;

    if(resolve_font(ovr_reg, regular_candidates, regular, sizeof(regular)) != 0)
        return VWM_SHOT_ERR_FONT;

    /* an explicit bold override is required if set; otherwise bold is
       optional and the renderer double-strikes when bold_path is NULL */
    if(ovr_bold != NULL && ovr_bold[0] != '\0')
    {
        if(resolve_font(ovr_bold, bold_candidates, bold, sizeof(bold)) != 0)
            return VWM_SHOT_ERR_FONT;
    }
    else if(resolve_font(NULL, bold_candidates, bold, sizeof(bold)) != 0)
    {
        bold[0] = '\0';
    }

    vk_screen_refresh(vwm->screen);

    if(target == VWM_SHOT_TOP)
    {
        capwin = vk_deck_get_top(vwm->deck);
        if(capwin != NULL)
            src = vk_widget_get_canvas(capwin);
    }

    if(src == NULL)
        src = vk_screen_get_window(vwm->screen);

    grid = vwmscrshot_capture(src);
    if(grid == NULL)
        return VWM_SHOT_ERR_ALLOC;

    cfg.font_path = regular;
    cfg.bold_path = (bold[0] != '\0') ? bold : NULL;
    cfg.font_px = SCRSHOT_PX;

    code = vwmscrshot_render(grid, &cfg, &rgb, &img_w, &img_h);
    if(code == VWM_SHOT_OK)
        code = vwmscrshot_write_png(path, rgb, img_w, img_h);

    if(rgb != NULL) free(rgb);
    vwmscrshot_grid_free(grid);

    return code;
}
