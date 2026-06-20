#ifndef _VWM_LAUNCH_PICKER_H_
#define _VWM_LAUNCH_PICKER_H_

#include "modules.h"

/*
    Modal file picker for the "%fd" launch token.

    A USER program whose params contain "%fd" has its raw argv stashed on
    module->fd_argv (see programs.c).  vwm_launch_picker_open() pops a
    file browser rooted at $HOME; when the user picks a file, "%fd" is
    substituted into a copy of that argv and the program is launched.
    Cancel / Esc / a click off the dialog aborts the launch.

    No-op if the module is NULL, has no fd_argv, or another modal tool is
    already open (the picker rides the shared vwm->tool_window slot).
*/
void vwm_launch_picker_open(vwm_module_t *module);

#endif
