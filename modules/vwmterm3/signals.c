#include <vwm.h>

#include "signals.h"

struct sigaction* vwmterm_sigset(int signum,sighandler_t handler)
{
   struct sigaction  *action;
   sigset_t          old_mask;

   if(handler==NULL) return NULL;

   action=(struct sigaction*)g_malloc0(sizeof(struct sigaction));

   /* retrieve current signal mask. */
   sigprocmask(0,NULL,&old_mask);
   action->sa_handler=handler;
   action->sa_mask=old_mask;
   sigaction(signum,(const struct sigaction*)action,NULL);

   g_free(action);

   return NULL;
}

void vwmterm_SIGIO(int signum)
{
	vwm_ui_accel(1);

   return;
}

