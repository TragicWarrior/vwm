#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"

/* read an entire file into a malloc'd, NUL-terminated buffer */
static char*
read_file(const char *path)
{
    FILE    *fp;
    long    size;
    char    *buf;
    size_t  got;

    fp = fopen(path, "rb");
    if(fp == NULL) return NULL;

    if(fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if(size < 0)
    {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    buf = malloc((size_t)size + 1);
    if(buf == NULL)
    {
        fclose(fp);
        return NULL;
    }

    got = fread(buf, 1, (size_t)size, fp);
    buf[got] = '\0';

    fclose(fp);

    return buf;
}

cJSON*
vwm_config_load(const char *path)
{
    char    *text;
    cJSON   *root;

    if(path == NULL) return NULL;

    text = read_file(path);
    if(text == NULL) return NULL;

    root = cJSON_Parse(text);
    free(text);

    return root;
}

int
vwm_config_store(const char *path, const cJSON *root)
{
    char    *text;
    FILE    *fp;

    if(path == NULL || root == NULL) return -1;

    text = cJSON_Print(root);
    if(text == NULL) return -1;

    fp = fopen(path, "wb");
    if(fp == NULL)
    {
        cJSON_free(text);
        return -1;
    }

    fputs(text, fp);
    fputc('\n', fp);
    fclose(fp);

    cJSON_free(text);

    return 0;
}

static void
add_program(cJSON *arr, const char *requires, const char *title,
    const char *bin, const char *type)
{
    cJSON   *p = cJSON_CreateObject();

    cJSON_AddStringToObject(p, "requires", requires);
    cJSON_AddStringToObject(p, "title", title);
    cJSON_AddStringToObject(p, "bin", bin);
    cJSON_AddStringToObject(p, "type", type);

    cJSON_AddItemToArray(arr, p);
}

cJSON*
vwm_config_defaults(void)
{
    cJSON   *root;
    cJSON   *hotkeys;
    cJSON   *programs;
    cJSON   *settings;

    root = cJSON_CreateObject();

    hotkeys = cJSON_AddObjectToObject(root, "hotkeys");
    cJSON_AddStringToObject(hotkeys, "menu",       "0000601b");
    cJSON_AddStringToObject(hotkeys, "wm",         "0000771b");
    cJSON_AddStringToObject(hotkeys, "close",      "00000011");
    cJSON_AddStringToObject(hotkeys, "cycle",      "00000009");
    cJSON_AddStringToObject(hotkeys, "move_up",    "00000103");
    cJSON_AddStringToObject(hotkeys, "move_down",  "00000102");
    cJSON_AddStringToObject(hotkeys, "move_left",  "00000104");
    cJSON_AddStringToObject(hotkeys, "move_right", "00000105");
    cJSON_AddStringToObject(hotkeys, "grow_h",     "0000002b");
    cJSON_AddStringToObject(hotkeys, "shrink_h",   "0000002d");
    cJSON_AddStringToObject(hotkeys, "grow_w",     "0000003e");
    cJSON_AddStringToObject(hotkeys, "shrink_w",   "0000003c");
    cJSON_AddStringToObject(hotkeys, "desktop",    "0000641b");

    programs = cJSON_AddArrayToObject(root, "programs");
    add_program(programs, "vterm-color",    "VTerm Color",   "/bin/bash", "Tool");
    add_program(programs, "vterm-xterm256", "VTerm HiColor", "/bin/bash", "Tool");
    add_program(programs, "vterm-vt100",    "VTerm VT100",   "/bin/sh",   "Tool");

    settings = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddStringToObject(settings, "task_indicator_action", "none");
    cJSON_AddStringToObject(settings, "date_click_action", "calendar");
    cJSON_AddNumberToObject(settings, "num_desktops", 3);
    cJSON_AddStringToObject(settings, "screensaver_command", "");
    cJSON_AddNumberToObject(settings, "screensaver_timeout", 0);

    return root;
}

const char*
vwm_json_str(const cJSON *obj, const char *key, const char *dflt)
{
    cJSON   *item;

    if(obj == NULL) return dflt;

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsString(item) && item->valuestring != NULL)
        return item->valuestring;

    return dflt;
}

int
vwm_json_int(const cJSON *obj, const char *key, int dflt)
{
    cJSON   *item;

    if(obj == NULL) return dflt;

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsNumber(item)) return item->valueint;

    return dflt;
}

int
vwm_json_bool(const cJSON *obj, const char *key, int dflt)
{
    cJSON   *item;

    if(obj == NULL) return dflt;

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsBool(item)) return cJSON_IsTrue(item) ? 1 : 0;

    return dflt;
}
