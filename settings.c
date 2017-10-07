#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vwm.h"
#include "settings.h"
#include "profile.h"
#include "private.h"
#include "hotkeys.h"

int
vwm_settings_load(char *rc_file)
{
    FILE    *file;
    char    *line_data;
    size_t  line_sz;
    ssize_t bytes_read;

    if(access(rc_file, R_OK) == -1) return ERR;
    line_data = NULL;

    file = fopen(rc_file, "r");
    do
    {
        if(line_data != NULL)
        {
            free(line_data);
            line_data = NULL;
        }

        line_sz = 0;
        bytes_read = getline(&line_data, &line_sz, file);
        if(bytes_read == -1 && line_sz == 0) break;

        if(line_data[0] == '#' || line_data[0] == ';') continue;
        if(vwm_settings_hotkey_load(line_data)!=ERR) continue;
        // if(vwm_settings_scrsaver_load(line_data)!=ERR) continue;
    }
    while(feof(file) == 0);

    fclose(file);
    return 0;
}

/*
gint vwm_settings_scrsaver_load(gchar *line_data)
{
   gchar *pos;
   glong timeout;

   pos=strstr(line_data,"screen_timeout");
   if(pos==NULL) return ERR;
   pos+=(strlen("screen_timeout"));

   line_data=pos;
   pos=strchr(line_data,'=');
   if(pos==NULL) return ERR;
   pos++;

   line_data=pos;
   g_strstrip(pos);
   timeout=strtol(pos,(char**)NULL,10);
   if(timeout==LONG_MIN || timeout==LONG_MAX) return ERR;
   if(timeout>90) timeout=90;

   vwm_scrsaver_timeout_set((gint)timeout);
   return 0;
}
*/

int
vwm_settings_hotkey_load(char *line_data)
{
    char    *pos;
    int32_t keystroke = 0;
    int     retval;

    pos = strstr(line_data, "menu_hotkey");
    if(pos == NULL) return ERR;
    pos += (strlen("menu_hotkey"));

    line_data = pos;
    pos = strchr(line_data, '=');
    if(pos == NULL) return ERR;
    pos++;

    line_data = pos;
    // g_strstrip(pos);
    retval = sscanf(pos, "%x", &keystroke);
    if(retval != 1) return 0;

    // vwm_hotkey_swap(VWM_HOTKEY_MENU,keystroke,1);
    return 0;
}



