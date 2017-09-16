#include <unistd.h>
#include <fcntl.h>

#include <vwm.h>
#include <vterm.h>

#include "signals.h"

void
vterm_init_sigio(vterm_t *vterm)
{
    static struct sigaction     s_action;
    int                         master_fd;
    int                         fflags;

    master_fd = vterm_get_pty_fd(vterm);

    // set up signal handler for SIGIO
    sigemptyset(&s_action.sa_mask);
    s_action.sa_sigaction = vwmterm_SIGIO;
    s_action.sa_flags = SA_SIGINFO;
#ifdef SIGPOLL
    sigaction(SIGPOLL, &s_action, NULL);
#else
    sigaction(SIGIO, &s_action, NULL);
#endif

    fcntl(master_fd,F_SETOWN,getpid());
    fflags = fcntl(master_fd,F_GETFL);
    fcntl(master_fd,F_SETFL,fflags | FASYNC);

    return;
}

void
vwmterm_SIGIO(int signum, siginfo_t *siginfo, void *uc)
{
    extern unsigned int wake_counter;

#ifdef SIGPOLL
    if(signum != SIGPOLL) return;
#else
    if(signum != SIGIO) return;
#endif

    // suppress the sleep thread if term is emitting output
    if (siginfo->si_code & POLL_OUT)
    {
        wake_counter++;
    }

    return;
}
