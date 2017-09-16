#ifndef _SIGNALS_H_
#define _SIGNALS_H_

#include <signal.h>

#include <vterm.h>

void vterm_init_sigio(vterm_t *vterm);

void vwmterm_SIGIO(int signum, siginfo_t *siginfo, void *uc);

#endif
