#include <sys/types.h>
#include <sys/wait.h>

#include <viper.h>
#include <vterm.h>

#include "vwmterm.h"
#include "events.h"

int
vwmterm_ON_KEYSTROKE(int32_t keystroke,WINDOW *window)
{
	vterm_t	*vterm;

	if(keystroke == KEY_MOUSE) return 1;

	vterm = (vterm_t*)viper_window_get_userptr(window);

    vterm_write_pipe(vterm,keystroke);

	return 1;
}


int
vwmterm_ON_RESIZE(WINDOW *window, void *anything)
{
	vterm_t         *vterm;
    unsigned int    width;
    unsigned int    height;

	vterm = (vterm_t*)anything;

	getmaxyx(window, height, width);
    vterm_resize(vterm, width, height);
    vterm_wnd_update(vterm);

	return 0;
}

int
vwmterm_ON_DESTROY(WINDOW *window, void *anything)
{
    vwmterm_data_t  *vwmterm_data;

    vwmterm_data = (vwmterm_data_t*)anything;

    vwmterm_data->state = VWMTERM_STATE_EXITING;

	return 0;
}

int
vwmterm_ON_CLOSE(WINDOW *window, void *anything)
{
	vterm_t	*vterm;
    pid_t    child_pid;

	vterm = (vterm_t*)anything;
    child_pid = vterm_get_pid(vterm);

	kill(child_pid, SIGKILL);
	waitpid(child_pid, NULL, 0);

	return VIPER_EVENT_WINDOW_DESIST;
}
