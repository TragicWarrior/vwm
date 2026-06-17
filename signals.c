#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _DEBUG
#include <execinfo.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include "vwm.h"
#include "signals.h"


/*
   Set on every SIGWINCH so the clock poller can tell a reattach happened.

   dtach's `-r winch` sends SIGWINCH to vwm when a client attaches.  ncurses
   only turns SIGWINCH into KEY_RESIZE when the terminal dimensions actually
   changed, so a reattach onto a same-size terminal never runs vwm's resync
   cascade (the freshly attached tty comes up with the mouse off, the cursor
   visible and keypad mode cleared).  The handler records the SIGWINCH here;
   check_destination_resize() in clock.c forces a KEY_RESIZE when this is set
   even though the geometry is unchanged.
*/
volatile sig_atomic_t   vwm_winch_pending = 0;

static struct sigaction vwm_winch_prev;
static int              vwm_winch_prev_valid = 0;

/*
   install handler for signum, preserving the current signal mask and
   leaving sa_flags clear (matches the old calloc-zeroed sigaction).
*/
void
vwm_sigset(int signum, sighandler_t handler)
{
    struct sigaction    action;
    sigset_t            old_mask;

    if(handler == NULL) return;

    memset(&action, 0, sizeof(action));

    // retrieve current signal mask
    sigprocmask(0,NULL,&old_mask);
    action.sa_handler = handler;
    action.sa_mask = old_mask;
    sigaction(signum, &action, NULL);
}

#ifdef _DEBUG
void vwm_backtrace(int signum)
{
    char                    *term_name=NULL;
    void                    *array[10];
    size_t                  count;
    char                    **strings;
    int                     fd=-1;
    size_t                  i;

    endwin();

    term_name = ctermid(NULL);
    if(term_name!=NULL)
    {
        fd = open(term_name, O_RDWR);
        if(fd != -1);
        dup2(fd, STDIN_FILENO);
    }

    count = backtrace(array, 10);
    strings = backtrace_symbols(array, count);

    printf("caught signal %d\n\r", signum);
    printf("obtained %zd stack frames.\n", count);

    for(i = 0;i < count;i++)
    {
        printf("%s\n\r", strings[i]);
    }

    free(strings);
    if(fd != -1) close(fd);
    exit(EXIT_FAILURE);

    return;
}
#endif

void
vwm_SIGIO(int signum)
{
    // noop for now

    (void)signum;
}

void
vwm_SIGWINCH(int signum)
{
    vwm_winch_pending = 1;

    /* chain to ncurses' handler (captured when we installed ours) so its
       own size-change -> KEY_RESIZE bookkeeping keeps working for ordinary
       resizes; we only add the same-size reattach case on top of it. */
    if(vwm_winch_prev_valid
        && (vwm_winch_prev.sa_flags & SA_SIGINFO) == 0
        && vwm_winch_prev.sa_handler != SIG_IGN
        && vwm_winch_prev.sa_handler != SIG_DFL
        && vwm_winch_prev.sa_handler != NULL)
    {
        vwm_winch_prev.sa_handler(signum);
    }

    (void)signum;
}

/*
   Install vwm_SIGWINCH, capturing whatever handler ncurses installed during
   newterm() so we can chain it.  Must be called AFTER curses init -- if it
   runs first, newterm()'s own handler setup overwrites ours.  ncurses only
   claims SIGWINCH when the current disposition is SIG_DFL, so once ours is
   in place it survives later newterm() calls (e.g. teleport).
*/
void
vwm_sigwinch_install(void)
{
    struct sigaction    act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = vwm_SIGWINCH;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;       /* no SA_RESTART: let SIGWINCH break the poll */

    if(sigaction(SIGWINCH, &act, &vwm_winch_prev) == 0)
        vwm_winch_prev_valid = 1;
}

void
vwm_SIGTERM(int signum)
{
    extern int  shutdown;

    (void)signum;

    /* let the main loop unwind so vk_screen_destroy can endwin() the
       active SCREEN -- the terminal would otherwise be left in raw
       mode (especially noticeable after teleport) */
    shutdown = 1;
}


