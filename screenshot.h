#ifndef _H_VWM_SCREENSHOT_
#define _H_VWM_SCREENSHOT_

/*
    In-process screenshot API.  The VWM-menu dialog is one caller;
    vwm-msg screenshot is another.
*/

enum
{
    VWM_SHOT_OK         = 0,
    VWM_SHOT_ERR_FONT   = 1,
    VWM_SHOT_ERR_GLYPH  = 2,
    VWM_SHOT_ERR_ALLOC  = 3,
    VWM_SHOT_ERR_PNG    = 4,
};

enum
{
    VWM_SHOT_SCREEN     = 0,    /* composed desktop (wallpaper, panel, …) */
    VWM_SHOT_TOP        = 1,    /* focused deck window, chrome included   */
};

/*
    Capture `target` to `path` as a PNG.  Refreshes the screen first so
    the painted surface is current.  Returns VWM_SHOT_OK or VWM_SHOT_ERR_*.
*/
int     vwm_screenshot_save(int target, const char *path);

/* open the interactive prompt + save dialog (VWM menu) */
void    vwm_screenshot_open(void);

#endif
