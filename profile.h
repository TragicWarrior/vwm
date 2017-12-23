#ifndef _VWM_PROFILE_H_
#define _VWM_PROFILE_H_

struct _vwm_profile_s
{
    uid_t   user;
    char    *login;
    char    *passwd;
    char    *home;
    char    *rc_file;
    char    *mod_dir;
};

#endif

