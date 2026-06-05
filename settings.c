#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "vwm.h"
#include "settings.h"
#include "profile.h"
#include "private.h"
#include "winman.h"

/*
    Hotkeys are stored flat under the "hotkeys" object as hex-string
    keycodes, e.g. { "menu": "0000601b", "wm": "0000771b", ... }.
*/
struct hotkey_entry
{
    const char  *name;
    int32_t     *field;
};

int
vwm_settings_load(vwm_t *vwm)
{
    cJSON       *root;
    cJSON       *hotkeys;
    const char  *value;
    int32_t     keystroke;
    int         i;
    int         count;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    struct hotkey_entry entries[] =
    {
        { "menu",       &vwm->hotkey_menu },
        { "wm",         &vwm->hotkey_wm },
        { "close",      &vwm->hotkey_close },
        { "cycle",      &vwm->hotkey_cycle },
        { "move_up",    &vwm->hotkey_move_up },
        { "move_down",  &vwm->hotkey_move_down },
        { "move_left",  &vwm->hotkey_move_left },
        { "move_right", &vwm->hotkey_move_right },
        { "grow_h",     &vwm->hotkey_grow_h },
        { "shrink_h",   &vwm->hotkey_shrink_h },
        { "grow_w",     &vwm->hotkey_grow_w },
        { "shrink_w",   &vwm->hotkey_shrink_w },
        { "desktop",    &vwm->hotkey_desktop },
    };

    count = sizeof(entries) / sizeof(entries[0]);

    root = vwm_config_load(vwm->profile->rc_file);
    if(root != NULL)
    {
        hotkeys = cJSON_GetObjectItemCaseSensitive(root, "hotkeys");

        for(i = 0; i < count; i++)
        {
            value = vwm_json_str(hotkeys, entries[i].name, NULL);
            if(value != NULL && sscanf(value, "%x", &keystroke) == 1)
                *entries[i].field = keystroke;
        }

        cJSON_Delete(root);
    }

    vwm_settings_load_general(vwm);

    return 0;
}

int
vwm_settings_save(vwm_t *vwm)
{
    cJSON   *root;
    cJSON   *hotkeys;
    char    buf[16];
    int     i;
    int     count;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    struct { const char *name; int32_t value; } keys[] =
    {
        { "menu",       vwm->hotkey_menu },
        { "wm",         vwm->hotkey_wm },
        { "close",      vwm->hotkey_close },
        { "cycle",      vwm->hotkey_cycle },
        { "move_up",    vwm->hotkey_move_up },
        { "move_down",  vwm->hotkey_move_down },
        { "move_left",  vwm->hotkey_move_left },
        { "move_right", vwm->hotkey_move_right },
        { "grow_h",     vwm->hotkey_grow_h },
        { "shrink_h",   vwm->hotkey_shrink_h },
        { "grow_w",     vwm->hotkey_grow_w },
        { "shrink_w",   vwm->hotkey_shrink_w },
        { "desktop",    vwm->hotkey_desktop },
    };

    count = sizeof(keys) / sizeof(keys[0]);

    root = vwm_config_load(vwm->profile->rc_file);
    if(root == NULL) root = cJSON_CreateObject();

    cJSON_DeleteItemFromObjectCaseSensitive(root, "hotkeys");
    hotkeys = cJSON_AddObjectToObject(root, "hotkeys");

    for(i = 0; i < count; i++)
    {
        snprintf(buf, sizeof(buf), "%08x", keys[i].value);
        cJSON_AddStringToObject(hotkeys, keys[i].name, buf);
    }

    vwm_config_store(vwm->profile->rc_file, root);
    cJSON_Delete(root);

    return 0;
}

void
vwm_settings_load_general(vwm_t *vwm)
{
    cJSON       *root;
    cJSON       *settings;
    const char  *str;
    int         val;

    if(vwm == NULL) return;
    if(vwm->profile == NULL) return;
    if(vwm->profile->rc_file == NULL) return;

    root = vwm_config_load(vwm->profile->rc_file);
    if(root == NULL) return;

    settings = cJSON_GetObjectItemCaseSensitive(root, "settings");

    str = vwm_json_str(settings, "task_indicator_action", NULL);
    if(str != NULL)
    {
        strncpy(vwm->task_indicator_action, str, NAME_MAX - 1);
        vwm->task_indicator_action[NAME_MAX - 1] = '\0';
    }

    str = vwm_json_str(settings, "date_click_action", NULL);
    if(str != NULL)
    {
        strncpy(vwm->date_click_action, str, NAME_MAX - 1);
        vwm->date_click_action[NAME_MAX - 1] = '\0';
    }

    val = vwm_json_int(settings, "num_desktops", 0);
    if(val >= 2 && val <= 6)
        vwm->surface_count = val;

    str = vwm_json_str(settings, "screensaver_command", NULL);
    if(str != NULL)
    {
        strncpy(vwm->screensaver_cmd, str, NAME_MAX - 1);
        vwm->screensaver_cmd[NAME_MAX - 1] = '\0';
    }

    val = vwm_json_int(settings, "screensaver_timeout", -1);
    if(val >= 0)
        vwm->screensaver_timeout = val;

    /* Host clipboard sync mode, stored under the display name
       ("Never"/"OSC 52"/"xclip"/"Both").  Unknown or missing values
       keep whatever vwm_init seeded. */
    str = vwm_json_str(settings, "clipboard", NULL);
    if(str != NULL)
    {
        int j;
        for(j = 0; j < VWM_CLIPBOARD_COUNT; j++)
        {
            if(strcmp(str, vwm_clipboard_mode_names[j]) == 0)
            {
                vwm->clipboard_mode = (short)j;
                break;
            }
        }
    }

    /* per-desktop colors and wallpapers -- both stored as string arrays
       (color and pattern names).  Missing or malformed entries keep
       whatever vwm_init seeded.  Unknown names also keep the default. */
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(settings,
            "desktop_colors");
        if(cJSON_IsArray(arr))
        {
            int n = cJSON_GetArraySize(arr);
            int i;
            if(n > VWM_MAX_DESKTOPS) n = VWM_MAX_DESKTOPS;
            for(i = 0; i < n; i++)
            {
                cJSON *item = cJSON_GetArrayItem(arr, i);
                int    j;
                if(!cJSON_IsString(item)) continue;
                for(j = 0; j < 16; j++)
                {
                    if(strcmp(item->valuestring, vwm_color_names[j]) == 0)
                    {
                        vwm->desktop_color[i] = (short)j;
                        break;
                    }
                }
            }
        }
    }
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(settings,
            "desktop_wallpapers");
        if(cJSON_IsArray(arr))
        {
            int n = cJSON_GetArraySize(arr);
            int i;
            if(n > VWM_MAX_DESKTOPS) n = VWM_MAX_DESKTOPS;
            for(i = 0; i < n; i++)
            {
                cJSON *item = cJSON_GetArrayItem(arr, i);
                int    j;
                if(!cJSON_IsString(item)) continue;
                for(j = 0; j < VWM_WALLPAPER_COUNT; j++)
                {
                    if(strcmp(item->valuestring,
                        vwm_wallpaper_names[j]) == 0)
                    {
                        vwm->desktop_wallpaper[i] = (short)j;
                        break;
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
}

void
vwm_settings_save_general(vwm_t *vwm)
{
    cJSON   *root;
    cJSON   *settings;

    if(vwm == NULL) return;
    if(vwm->profile == NULL) return;
    if(vwm->profile->rc_file == NULL) return;

    root = vwm_config_load(vwm->profile->rc_file);
    if(root == NULL) root = cJSON_CreateObject();

    cJSON_DeleteItemFromObjectCaseSensitive(root, "settings");
    settings = cJSON_AddObjectToObject(root, "settings");

    cJSON_AddStringToObject(settings, "task_indicator_action",
        vwm->task_indicator_action);
    cJSON_AddStringToObject(settings, "date_click_action",
        vwm->date_click_action);
    cJSON_AddNumberToObject(settings, "num_desktops", vwm->surface_count);
    cJSON_AddStringToObject(settings, "screensaver_command",
        vwm->screensaver_cmd);
    cJSON_AddNumberToObject(settings, "screensaver_timeout",
        vwm->screensaver_timeout);
    {
        int idx = vwm->clipboard_mode;
        if(idx < 0 || idx >= VWM_CLIPBOARD_COUNT)
            idx = VWM_CLIPBOARD_NEVER;
        cJSON_AddStringToObject(settings, "clipboard",
            vwm_clipboard_mode_names[idx]);
    }

    /* persist the full VWM_MAX_DESKTOPS slot range so that shrinking
       num_desktops and then growing it again keeps the user's earlier
       picks.  Defensive clamp on the stored index in case the field
       was corrupted in memory. */
    {
        cJSON *arr = cJSON_AddArrayToObject(settings, "desktop_colors");
        int i;
        for(i = 0; i < VWM_MAX_DESKTOPS; i++)
        {
            int idx = vwm->desktop_color[i];
            if(idx < 0 || idx > 15) idx = COLOR_BLUE;
            cJSON_AddItemToArray(arr,
                cJSON_CreateString(vwm_color_names[idx]));
        }
    }
    {
        cJSON *arr = cJSON_AddArrayToObject(settings,
            "desktop_wallpapers");
        int i;
        for(i = 0; i < VWM_MAX_DESKTOPS; i++)
        {
            int idx = vwm->desktop_wallpaper[i];
            if(idx < 0 || idx >= VWM_WALLPAPER_COUNT)
                idx = VWM_WALLPAPER_STIPLE;
            cJSON_AddItemToArray(arr,
                cJSON_CreateString(vwm_wallpaper_names[idx]));
        }
    }

    vwm_config_store(vwm->profile->rc_file, root);
    cJSON_Delete(root);
}
