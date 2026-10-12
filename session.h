#ifndef _H_VWM_SESSION_
#define _H_VWM_SESSION_

/*
    Which session.

    A session is known by the path of its control socket and by nothing
    else: vwm listens there, and vwm-msg (and through it vwm-resume and
    vwm-stop) connects there.  The path is $VWM_CONTROL_SOCK when that
    is set, otherwise ~/.config/vwm/control.sock.

    A session id is a name for one of those paths, so that several
    sessions can be run and told apart without spelling the paths out:

        vwm --sid work          vwm-resume --sid work

    No id, or the id "0", is the default session -- the path that has
    always been used.  Any other id is control-<id>.sock beside it.

    The option works by setting VWM_CONTROL_SOCK in the program that
    was given it.  Everything else already goes by that variable, and a
    session passes it on to the programs it starts, so vwm-msg run
    inside a session reaches that session without being told.
*/

/* the default session's id */
#define VWM_SESSION_DEFAULT     "0"

/* the longest a session id may be */
#define VWM_SESSION_SID_MAX     32

/* is `sid` usable as a session id?  letters, digits, '-' and '_', one
   to VWM_SESSION_SID_MAX of them: it becomes part of a file name */
int     vwm_session_sid_valid(const char *sid);

/*
    Make `sid` the session this program talks to (or is), by pointing
    VWM_CONTROL_SOCK at its socket.  An id given this way wins over a
    VWM_CONTROL_SOCK that was inherited.  Returns 0, or -1 when the id
    is not valid or there is no home directory to put the socket in.
*/
int     vwm_session_select(const char *sid);

/*
    The words to add to a command so that it reaches the session this
    program is using: " --sid ID" for a named session, "" for the
    default one.  Worked out from VWM_CONTROL_SOCK, so it is right
    whether the id was given here or the variable was inherited from
    the session itself.  For messages that tell the user what to type.
    Also "" for a socket path that was set by hand and names no id.
*/
void    vwm_session_hint(char *buf, size_t n);

/*
    Look for "--sid ID" or "--sid=ID" among the first `scan` arguments
    after the program's name, act on it (vwm_session_select) and take
    it out of argv so the rest of the program need not know about it.
    *argc is updated.  `prog` names the program in an error message.

    Returns 0 when there was no such option or it was applied, and -1
    when it was there but could not be used -- a message has then been
    written to stderr.
*/
int     vwm_session_args(int *argc, char **argv, int scan, const char *prog);

#endif
