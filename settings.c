#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libconfig.h>

#include "vwm.h"
#include "settings.h"
#include "profile.h"
#include "private.h"
#include "winman.h"

struct hotkey_entry
{
    const char  *path;
    int32_t     *field;
};

static void
load_hotkey(config_t *config, const char *path, int32_t *field)
{
    const char  *value;
    int32_t     keystroke;
    int         retval;

    retval = config_lookup_string(config, path, &value);

    if(retval == CONFIG_TRUE)
    {
        retval = sscanf(value, "%x", &keystroke);
        if(retval == 1) *field = keystroke;
    }
}

int
vwm_settings_load(vwm_t *vwm)
{
    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    config_init(&vwm->config);
    config_read_file(&vwm->config, vwm->profile->rc_file);

    struct hotkey_entry entries[] =
    {
        { "hotkeys.menu.key",                       &vwm->hotkey_menu },
        { "hotkeys.window_management.wm.key",       &vwm->hotkey_wm },
        { "hotkeys.window_management.close.key",    &vwm->hotkey_close },
        { "hotkeys.window_management.cycle.key",    &vwm->hotkey_cycle },
        { "hotkeys.window_management.move_up.key",  &vwm->hotkey_move_up },
        { "hotkeys.window_management.move_down.key",&vwm->hotkey_move_down },
        { "hotkeys.window_management.move_left.key",&vwm->hotkey_move_left },
        { "hotkeys.window_management.move_right.key",
                                                    &vwm->hotkey_move_right },
        { "hotkeys.window_management.grow_h.key",   &vwm->hotkey_grow_h },
        { "hotkeys.window_management.shrink_h.key", &vwm->hotkey_shrink_h },
        { "hotkeys.window_management.grow_w.key",   &vwm->hotkey_grow_w },
        { "hotkeys.window_management.shrink_w.key", &vwm->hotkey_shrink_w },
        { "hotkeys.navigation.desktop.key",         &vwm->hotkey_desktop },
    };

    int count = sizeof(entries) / sizeof(entries[0]);

    for(int i = 0; i < count; i++)
    {
        load_hotkey(&vwm->config, entries[i].path, entries[i].field);
    }

    config_destroy(&vwm->config);

    return 0;
}

int
vwm_settings_save(vwm_t *vwm)
{
    config_t        config;
    config_setting_t *root;
    config_setting_t *hotkeys;
    config_setting_t *group;
    config_setting_t *entry;
    config_setting_t *setting;
    char            buf[16];

    if(vwm == NULL) return -1;
    if(vwm->profile == NULL) return -1;
    if(vwm->profile->rc_file == NULL) return -1;

    config_init(&config);
    config_read_file(&config, vwm->profile->rc_file);

    root = config_root_setting(&config);

    hotkeys = config_lookup(&config, "hotkeys");
    if(hotkeys != NULL)
        config_setting_remove(root, "hotkeys");

    hotkeys = config_setting_add(root, "hotkeys", CONFIG_TYPE_GROUP);

    // menu accelerators
    group = config_setting_add(hotkeys, "menu", CONFIG_TYPE_GROUP);
    snprintf(buf, sizeof(buf), "%08x", vwm->hotkey_menu);
    setting = config_setting_add(group, "key", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, buf);

    // window management
    group = config_setting_add(hotkeys, "window_management",
        CONFIG_TYPE_GROUP);

    struct { const char *name; int32_t value; } wm_keys[] =
    {
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
    };

    for(int i = 0; i < (int)(sizeof(wm_keys) / sizeof(wm_keys[0])); i++)
    {
        entry = config_setting_add(group, wm_keys[i].name,
            CONFIG_TYPE_GROUP);
        snprintf(buf, sizeof(buf), "%08x", wm_keys[i].value);
        setting = config_setting_add(entry, "key", CONFIG_TYPE_STRING);
        config_setting_set_string(setting, buf);
    }

    // navigation
    group = config_setting_add(hotkeys, "navigation", CONFIG_TYPE_GROUP);
    entry = config_setting_add(group, "desktop", CONFIG_TYPE_GROUP);
    snprintf(buf, sizeof(buf), "%08x", vwm->hotkey_desktop);
    setting = config_setting_add(entry, "key", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, buf);

    config_write_file(&config, vwm->profile->rc_file);
    config_destroy(&config);

    return 0;
}
