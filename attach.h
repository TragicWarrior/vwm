#ifndef _H_VWM_ATTACH_
#define _H_VWM_ATTACH_

/*
    The terminal side of an attached session.

    vwm runs in the background, on no terminal.  To show it on one, a
    small program on that terminal asks the session to come over and
    then simply waits, as the shell's foreground job, until the session
    lets go again -- it was detached, it moved to another terminal, or
    it ended.  While it waits it reads nothing and draws nothing: vwm
    drives the terminal directly.  Its only work is to be there, which
    is what keeps the shell from reading the keyboard at the same time,
    and to hand the terminal back in good order afterwards.

    `vwm` (after starting the session) and `vwm-msg attach` both do this
    through vwm_attach_run().
*/

/*
    Bring the session listening on control socket `sock` to terminal
    `tty`, driven as terminal type `term` (NULL: let vwm decide), and
    wait until it leaves.  Says what happened on the terminal.

    Returns a process exit status: 0 when the session left in an
    orderly way, 1 when it refused the terminal or was lost.
*/
int     vwm_attach_run(const char *sock, const char *tty, const char *term);

#endif
