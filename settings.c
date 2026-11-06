#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "vwm.h"
#include "bkgd.h"
#include "settings.h"
#include "profile.h"
#include "private.h"
#include "mainmenu.h"
#include "winman.h"

/*
    Hotkeys are stored flat under the "hotkeys" object as hex-string
    keycodes, e.g. { "menu": "0000601b", "wm": "0000771b", ... }.
    This is the only list of names, offsets and defaults.  The dialog's
    labels sit beside it, in manage_hotkeys.c, in the same order.
*/
const vwm_hotkey_def_t vwm_hotkeys[VWM_HOTKEY_COUNT] =
{
    { "menu",       offsetof(vwm_t, hotkey_menu),       VWM_HOTKEY_MENU },
    { "wm",         offsetof(vwm_t, hotkey_wm),         VWM_HOTKEY_WM },
    { "close",      offsetof(vwm_t, hotkey_close),      17 },
    { "cycle",      offsetof(vwm_t, hotkey_cycle),      KEY_TAB },
    { "move_up",    offsetof(vwm_t, hotkey_move_up),    KEY_UP },
    { "move_down",  offsetof(vwm_t, hotkey_move_down),  KEY_DOWN },
    { "move_left",  offsetof(vwm_t, hotkey_move_left),  KEY_LEFT },
    { "move_right", offsetof(vwm_t, hotkey_move_right), KEY_RIGHT },
    { "grow_h",     offsetof(vwm_t, hotkey_grow_h),     '+' },
    { "shrink_h",   offsetof(vwm_t, hotkey_shrink_h),   '-' },
    { "grow_w",     offsetof(vwm_t, hotkey_grow_w),     '>' },
    { "shrink_w",   offsetof(vwm_t, hotkey_shrink_w),   '<' },
    { "desktop",    offsetof(vwm_t, hotkey_desktop),    (27 | (100 << 8)) },
    { "detach",     offsetof(vwm_t, hotkey_detach),     28 },   /* Ctrl-\ */
};

/* see settings.h */
int32_t *
vwm_hotkey_field(vwm_t *vwm, int index)
{
    return (int32_t *)((char *)vwm + vwm_hotkeys[index].offset);
}

/*
    Apply the "hotkeys" object of a tree the caller parsed.  The caller
    frees `root`.  A key the file does not name, or a value sscanf does
    not parse, keeps the value it has.
*/
int
vwm_settings_load_hotkeys_from_json(vwm_t *vwm, const cJSON *root)
{
    cJSON       *hotkeys;
    const char  *value;
    int32_t     keystroke;
    int         i;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    /* no file: keep the values currently in effect */
    if(root == NULL) return 0;

    hotkeys = cJSON_GetObjectItemCaseSensitive(root, "hotkeys");

    /* a name the file leaves out keeps the value it already has */
    for(i = 0; i < VWM_HOTKEY_COUNT; i++)
    {
        value = vwm_json_str(hotkeys, vwm_hotkeys[i].name, NULL);
        if(value != NULL && sscanf(value, "%x", &keystroke) == 1)
            *vwm_hotkey_field(vwm, i) = keystroke;
    }

    return 0;
}

/*
    Read the hotkeys from the config file into the running session.
*/
int
vwm_settings_load_hotkeys(vwm_t *vwm)
{
    cJSON   *root;
    int     status;

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    root = vwm_config_load(vwm->profile->rc_file);
    status = vwm_settings_load_hotkeys_from_json(vwm, root);
    if(root != NULL) cJSON_Delete(root);

    return status;
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

    count = VWM_HOTKEY_COUNT;

    root = vwm_config_load(vwm->profile->rc_file);
    if(root == NULL) root = cJSON_CreateObject();

    cJSON_DeleteItemFromObjectCaseSensitive(root, "hotkeys");
    hotkeys = cJSON_AddObjectToObject(root, "hotkeys");

    for(i = 0; i < count; i++)
    {
        snprintf(buf, sizeof(buf), "%08x", *vwm_hotkey_field(vwm, i));
        cJSON_AddStringToObject(hotkeys, vwm_hotkeys[i].name, buf);
    }

    vwm_config_store(vwm->profile->rc_file, root);
    cJSON_Delete(root);

    return 0;
}

/*
    Apply the "settings" object of a tree the caller parsed.  The
    caller frees `root`.  A missing object leaves every value as it
    is: the json helpers then return defaults that fail the range
    checks below.
*/
void
vwm_settings_load_general_from_json(vwm_t *vwm, const cJSON *root)
{
    cJSON       *settings;
    const char  *str;
    int         val;

    if(vwm == NULL) return;
    if(vwm->profile == NULL) return;
    if(vwm->profile->rc_file == NULL) return;

    /* no file: keep the values currently in effect */
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
       ("Never"/"OSC 52"/"xclip"/"Both").  An unknown or missing value
       keeps the mode currently in effect. */
    str = vwm_json_str(settings, "clipboard", NULL);
    if(str != NULL)
    {
        int j;

        /* an unknown name keeps the current mode */
        j = vwm_name_index(vwm_clipboard_mode_names,
            VWM_CLIPBOARD_COUNT, str);
        if(j >= 0) vwm->clipboard_mode = (short)j;
    }

    /* bottom-left hostname label: on/off flag plus its fg/bg colors
       (stored as color names like the desktop colors). */
    val = vwm_json_int(settings, "show_hostname", -1);
    if(val == 0 || val == 1)
        vwm->show_hostname = (short)val;

    str = vwm_json_str(settings, "hostname_fg", NULL);
    if(str != NULL)
    {
        int j;

        /* an unknown colour name keeps the current foreground */
        j = vwm_name_index(vwm_color_names, 16, str);
        if(j >= 0) vwm->hostname_fg = (short)j;
    }

    str = vwm_json_str(settings, "hostname_bg", NULL);
    if(str != NULL)
    {
        int j;

        /* an unknown colour name keeps the current background */
        j = vwm_name_index(vwm_color_names, 16, str);
        if(j >= 0) vwm->hostname_bg = (short)j;
    }

    /* big-font hostname: font is a vwmfont size index (-1 = Basic), fill
       is a vwmfont_fill_t (0..2).  Out-of-range size is handled by the
       renderer (falls back to the plain label). */
    val = vwm_json_int(settings, "hostname_font", -2);
    if(val >= -1) vwm->hostname_font = (short)val;
    val = vwm_json_int(settings, "hostname_fill", -1);
    if(val >= 0 && val <= 2) vwm->hostname_fill = (short)val;

    /* per-desktop colors and wallpapers -- both stored as string arrays
       (color and pattern names).  A missing, malformed, or unknown
       entry keeps the value currently in effect. */
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

                /* an unknown name keeps this desktop's current colour */
                j = vwm_name_index(vwm_color_names, 16, item->valuestring);
                if(j >= 0) vwm->desktop_color[i] = (short)j;
            }
        }
    }
    {
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(settings,
            "desktop_fgs");
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

                /* an unknown name keeps this desktop's current foreground */
                j = vwm_name_index(vwm_color_names, 16, item->valuestring);
                if(j >= 0) vwm->desktop_fg[i] = (short)j;
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

                /* an unknown name keeps this desktop's current pattern */
                j = vwm_name_index(vwm_wallpaper_names,
                    VWM_WALLPAPER_COUNT, item->valuestring);
                if(j >= 0) vwm->desktop_wallpaper[i] = (short)j;
            }
        }
    }
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

    cJSON_AddNumberToObject(settings, "show_hostname",
        vwm->show_hostname ? 1 : 0);
    {
        int idx = vwm->hostname_fg;
        if(idx < 0 || idx > 15) idx = COLOR_WHITE;
        cJSON_AddStringToObject(settings, "hostname_fg",
            vwm_color_names[idx]);
    }
    {
        int idx = vwm->hostname_bg;
        if(idx < 0 || idx > 15) idx = COLOR_BLUE;
        cJSON_AddStringToObject(settings, "hostname_bg",
            vwm_color_names[idx]);
    }
    cJSON_AddNumberToObject(settings, "hostname_font", vwm->hostname_font);
    cJSON_AddNumberToObject(settings, "hostname_fill", vwm->hostname_fill);

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
        cJSON *arr = cJSON_AddArrayToObject(settings, "desktop_fgs");
        int i;
        for(i = 0; i < VWM_MAX_DESKTOPS; i++)
        {
            int idx = vwm->desktop_fg[i];
            if(idx < 0 || idx > 15) idx = COLOR_BLACK;
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
