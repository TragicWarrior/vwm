#ifndef _VWM_SIGNALS_H_
#define _VWM_SIGNALS_H_

#include <signal.h>

extern volatile sig_atomic_t vwm_winch_pending;

void vwm_sigset(int signum, sighandler_t handler);

#ifdef VWM_CRASH_BACKTRACE
/* Install SIGSEGV/ABRT/BUS/FPE/ILL handlers that dump a glibc backtrace
   to /tmp/vwm-crash-<epoch>-<pid>.log (cmake -DVWM_CRASH_BACKTRACE=ON). */
void    vwm_crash_handlers_install(void);
#else
static inline void
vwm_crash_handlers_install(void)
{
}
#endif

void 	vwm_SIGIO(int signum);
void 	vwm_SIGTERM(int signum);
void 	vwm_SIGWINCH(int signum);
void 	vwm_sigwinch_install(void);

#endif  

