#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "strings.h"
#include "private.h"
#include "vwm.h"
#include "cJSON.h"
#include "config.h"

static int
_vwm_create_rc_file(vwm_profile_t *profile);

int
vwm_profile_init(vwm_t *vwm)
{
    static vwm_profile_t    *profile = NULL;
    struct passwd           *user_info;
    struct stat             stat_info;
    char                    *buffer;
    int                     i;

    profile = (vwm_profile_t*)calloc(1, sizeof(vwm_profile_t));
    profile->user = getuid();

    user_info = getpwuid(profile->user);
    if(user_info != NULL)
    {
        profile->login = strdup(user_info->pw_name);
        profile->home = strdup(user_info->pw_dir);
    }

    /* try to get home dir from environment if getpwuid()->pw_passed fails. */
    if(profile->home == NULL) profile->home = strdup(getenv("HOME"));

    /* make sure home directory path is formatted correctly. */
    i = strlen(profile->home);
    do
    {
        if(i < 2) break;
        if(profile->home[i - 1] == '/') profile->home[i - 1] = '\0';
        i = strlen(profile->home);
    }
    while(profile->home[i - 1] == '/');

    /* check to see if ~/.config/vwm/config.json exists. */
    buffer = strdup_printf("%s/.config/vwm/config.json", profile->home);
    if(stat(buffer, &stat_info) == 0)
    {
        // if it exists, make sure its readable
        if(!(S_ISREG(stat_info.st_mode)))
        {
            free(buffer);
            return -1;
        }
    }
    else
    {
        if(_vwm_create_rc_file(profile) == -1)
        {
            free(buffer);
            return -1;
        }
    }

    profile->rc_file = buffer;

    /* check to see if ~/.vwm/modules directory exists. */
    buffer = strdup_printf("%s/.vwm/modules", profile->home);
    if(stat(buffer, &stat_info) == 0)
    {
        if(S_ISDIR(stat_info.st_mode)) profile->mod_dir = buffer;
        else free(buffer);
    }
    else free(buffer);

    vwm->profile = profile;

    return 0;
}

char*
vwm_profile_mod_dir_get(vwm_t *vwm)
{
    if(vwm == NULL) return NULL;
    if(vwm->profile == NULL) return NULL;

    return vwm->profile->mod_dir;
}

char*
vwm_profile_login_get(vwm_t *vwm)
{
    if(vwm == NULL) return NULL;
    if(vwm->profile == NULL) return NULL;

    return vwm->profile->login;
}

char*
vwm_profile_rc_file_get(vwm_t *vwm)
{
    if(vwm == NULL) return NULL;
    if(vwm->profile == NULL) return NULL;

    return vwm->profile->rc_file;
}

static int
_vwm_create_rc_file(vwm_profile_t *profile)
{
    cJSON   *root;
    char    *dir;
    char    *path;
    int     retval;

    if(profile == NULL) return -1;
    if(profile->home == NULL) return -1;

    /* ensure ~/.config exists, then ~/.config/vwm */
    dir = strdup_printf("%s/.config", profile->home);
    if(mkdir(dir, 0755) == -1 && errno != EEXIST)
    {
        free(dir);
        return -1;
    }
    free(dir);

    dir = strdup_printf("%s/.config/vwm", profile->home);
    if(mkdir(dir, 0755) == -1 && errno != EEXIST)
    {
        free(dir);
        return -1;
    }
    free(dir);

    path = strdup_printf("%s/.config/vwm/config.json", profile->home);

    root = vwm_config_defaults();
    retval = vwm_config_store(path, root);
    cJSON_Delete(root);

    free(path);

    return retval;
}
