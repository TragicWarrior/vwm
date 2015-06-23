#ifndef _VMW_SIGNALS_H_
#define _VWM_SIGNALS_H_

#include <signal.h>

struct sigaction* vwm_sigset(int signum,sighandler_t handler);

void 	vwm_backtrace(int signum);
void 	vwm_SIGIO(int signum);

#endif  

