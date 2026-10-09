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
vwm_SIGTERM(int signum)
{
    extern int  shutdown;

    (void)signum;

    /* let the main loop unwind so vk_screen_destroy can endwin() the
       active SCREEN -- the terminal would otherwise be left in raw
       mode (especially noticeable after teleport) */
    shutdown = 1;
}


