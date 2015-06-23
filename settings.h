#ifndef _VWM_SETTINGS_H_
#define _VWM_SETTINGS_H_

#include <glib.h>

gint  vwm_settings_load(gchar *rc_file);
// gint  vwm_settings_scrsaver_load(gchar *line_data);
gint  vwm_settings_hotkey_load(gchar *line_data);
// gint  vwm_settings_bkgd_load(gchar *line_data);

#endif
