#ifndef _SIGNALS_H_
#define _SIGNALS_H_

#include <signal.h>

struct sigaction* vwmterm_sigset(int signum,sighandler_t handler);

void	vwmterm_SIGIO(int signum);

#endif

