#ifndef _VWM_CLOCK_H_
#define _VWM_CLOCK_H_

#include <signal.h>
#include <time.h>

#include "protothread.h"

struct _clock_data_s
{
    timer_t             timer_id;
    struct sigevent     s_event;
    struct sigaction    s_action;
    struct itimerspec   itimer;
};

typedef struct _clock_data_s    clock_data_t;

pt_t            vwm_clock_driver(void * const env);
clock_data_t*   vwm_clock_init(void);
void            vwm_clock_driver_SIGALARM(int signum, siginfo_t *siginfo, void *uc);

#endif
