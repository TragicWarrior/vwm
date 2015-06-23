#include <sys/types.h>
#include <sys/wait.h>

#include <viper.h>
#include <vterm.h>

#include "events.h"

gint vwmterm_ON_KEYSTROKE(gint32 keystroke,WINDOW *window)
{
	vterm_t	*vterm;

	if(keystroke==KEY_MOUSE) return 1;

	viper_thread_enter();
	vterm=(vterm_t*)viper_window_get_userptr(window);
	viper_thread_leave();

   vterm_write_pipe(vterm,keystroke);

	return 1;
}


gint vwmterm_ON_RESIZE(WINDOW *window,gpointer anything)
{
	vterm_t  *vterm;
   guint    width;
   guint    height;

	vterm=(vterm_t*)anything;

	viper_thread_enter();

	getmaxyx(window,height,width);
   vterm_resize(vterm,width,height);
   vterm_wnd_update(vterm);

	viper_thread_leave();

	return 0;
}

gint vwmterm_ON_DESTROY(WINDOW *window,gpointer anything)
{
	vterm_t	   *vterm;

   vterm=(vterm_t*)anything;
   vterm_destroy(vterm);

	return 0;
}

gint vwmterm_ON_CLOSE(WINDOW *window,gpointer anything)
{
	vterm_t	*vterm;
   pid_t    child_pid;

	vterm=(vterm_t*)anything;
   child_pid=vterm_get_pid(vterm);

	kill(child_pid,SIGKILL);
	waitpid(child_pid,NULL,0);

	return VIPER_EVENT_WINDOW_DESIST;
}
