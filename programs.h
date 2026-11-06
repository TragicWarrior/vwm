#ifndef _VWM_PROGRAMS_H_
#define _VWM_PROGRAMS_H_

#include "cJSON.h"
#include "vwm.h"
#include "profile.h"

/*
    Fields of one programs[] object.  The string pointers alias the
    JSON tree and die with it.  title and bin are "" when that key is
    missing; requires and params are NULL.  type is Misc when the key
    is missing or not a known type name.
*/
typedef struct
{
    const char  *requires;
    const char  *title;
    const char  *bin;
    const char  *params;
    int         type;
    int         hidden;
    int         scrollback;
    int         start_home;
    int         term_width;
    int         term_height;
}
vwm_program_fields_t;

/*
    Decode one programs[] object into `out`.  Returns 0 when `entry`
    is an object, -1 otherwise (the caller skips it).  A missing
    "requires" is not a reason to reject the object: the menu loader
    skips those, the Apps dialog keeps them.
*/
int vwm_program_from_json(const cJSON *entry, vwm_program_fields_t *out);

int vwm_programs_load(vwm_t *vwm);

/*
    Add the programs in a tree the caller already parsed.  The caller
    frees `root`.  Returns -1 when vwm or its rc path is missing, when
    `root` is NULL, or when "programs" is not an array.  Does not
    remove programs that are already loaded.
*/
int vwm_programs_load_from_json(vwm_t *vwm, const cJSON *root);

int vwm_programs_reload(void);

#endif
