#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <viper.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"

int
vwmterm_ON_KEYSTROKE(int32_t keystroke, vwnd_t *vwnd)
{
	vterm_t	*vterm;

	if(keystroke == KEY_MOUSE) return 1;

	vterm = (vterm_t*)viper_window_get_userptr(vwnd);

    vterm_write_pipe(vterm, keystroke);

	// return 1;

    return KMIO_HANDLED;
}

int
vwmterm_ON_SCREEN_RESIZED(vwnd_t *vwnd, void *anything)
{
    if(anything != NULL)
    {
        if(strcmp((char *)anything, "fullscreen") == 0)
        {
            viper_wresize_abs(vwnd, WSIZE_FULLSCREEN, WSIZE_FULLSCREEN);
        }
    }

    return 0;
}


int
vwmterm_ON_RESIZE(vwnd_t *vwnd, void *anything)
{
	vterm_t         *vterm;
    unsigned int    width;
    unsigned int    height;

	vterm = (vterm_t*)anything;

	getmaxyx(VWINDOW(vwnd), height, width);
    vterm_resize(vterm, width, height);
    vterm_wnd_update(vterm, -1, 0);

	return 0;
}

int
vwmterm_ON_CLOSE(vwnd_t *vwnd, void *anything)
{
    vwmterm_data_t  *vwmterm_data;
    pid_t           child_pid;

    (void)vwnd;

    vwmterm_data = (vwmterm_data_t*)anything;

    // tell the pseudo thread we're shutting down
    vwmterm_data->state = VWMTERM_STATE_EXITING;

    child_pid = vterm_get_pid(vwmterm_data->vterm);

	kill(child_pid, SIGKILL);
	waitpid(child_pid, NULL, 0);

	return 0;
}
