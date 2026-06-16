#ifndef _VWM_SIGNALS_H_
#define _VWM_SIGNALS_H_

#include <signal.h>

extern volatile sig_atomic_t vwm_winch_pending;

struct sigaction* vwm_sigset(int signum, sighandler_t handler);

void 	vwm_backtrace(int signum);
void 	vwm_SIGIO(int signum);
void 	vwm_SIGTERM(int signum);
void 	vwm_SIGWINCH(int signum);
void 	vwm_sigwinch_install(void);

#endif  

