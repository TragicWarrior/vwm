#ifndef _VWM_SETTINGS_H_
#define _VWM_SETTINGS_H_

#include "vwm.h"

int     vwm_settings_load(vwm_t *vwm);
int     vwm_settings_save(vwm_t *vwm);
int     vwm_settings_hotkey_load(char *line_data);

void    vwm_settings_load_general(vwm_t *vwm);
void    vwm_settings_save_general(vwm_t *vwm);

// gint  vwm_settings_bkgd_load(gchar *line_data);
// gint  vwm_settings_scrsaver_load(gchar *line_data);

#endif
