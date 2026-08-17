#ifndef _H_VWM_CTL_
#define _H_VWM_CTL_

/*
    In-process control plane.  vwm-msg talks to the listen socket
    (VWM_CONTROL_SOCK, else ~/.config/vwm/control.sock).
*/

int     vwm_ctl_init(void);
void    vwm_ctl_poll(void);
void    vwm_ctl_shutdown(void);
int     vwm_ctl_listen_fd(void);

#endif
