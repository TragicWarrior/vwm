#include <stdio.h>
#include <stdlib.h>
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
   pass back the struct sigaction so that the memory can be release if we
   need to replace the sighandler.  not use now, but maybe useful in the
   future.
*/
struct sigaction*
vwm_sigset(int signum, sighandler_t handler)
{
    struct sigaction    *action;
    sigset_t            old_mask;

    if(handler == NULL) return NULL;

    action = (struct sigaction*)calloc(1,sizeof(struct sigaction));

    // retrieve current signal mask
    sigprocmask(0,NULL,&old_mask);
    action->sa_handler = handler;
    action->sa_mask = old_mask;
    sigaction(signum,(const struct sigaction*)action,NULL);

    free(action);

    (void)signum;

    return NULL;
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


