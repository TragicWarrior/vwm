#ifndef _H_VWM_CTL_
#define _H_VWM_CTL_

/*
    In-process control plane.  vwm-msg talks to the listen socket
    (VWM_CONTROL_SOCK, else ~/.config/vwm/control.sock).
*/

int     vwm_ctl_init(void);
int     vwm_ctl_preflight(char *pathbuf, size_t n);
void    vwm_ctl_poll(void);
void    vwm_ctl_shutdown(void);
int     vwm_ctl_listen_fd(void);

/*
    The session has left the terminal it was showing on: tell the client
    waiting there (attach.h) why, and let it go.  `reason` is one of
    "detached", "moved", "stopped".  Call it once the terminal is back
    in order -- the client exits on this and its shell takes over.  Does
    nothing when no client is waiting.
*/
void    vwm_ctl_release_client(const char *reason);

#endif
