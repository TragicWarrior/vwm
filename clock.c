#include <time.h>

#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "clock.h"
#include "private.h"

clock_data_t*
vwm_clock_init(void)
{
    clock_data_t    *clock_data;

    clock_data = calloc(1, sizeof(clock_data_t));

    // set up signal handler for clock with SIGALRM
    sigemptyset(&clock_data->s_action.sa_mask);
    clock_data->s_action.sa_sigaction = vwm_clock_driver_SIGALARM;
    clock_data->s_action.sa_flags = SA_SIGINFO;
    sigaction(SIGALRM, &clock_data->s_action, NULL);

    // setup timer
    // clock_data->s_event.sigev_notify = SIGEV_SIGNAL;
    // clock_data->s_event.sigev_signo = SIGALRM;
    // clock_data->s_event.sigev_value.sival_ptr = &clock_data->timer_id;
    clock_data->itimer.it_value.tv_sec = 0;
    clock_data->itimer.it_value.tv_nsec = VWM_TICK_FREQ;      // 1/10 sec
    clock_data->itimer.it_interval.tv_sec = 0;
    clock_data->itimer.it_interval.tv_nsec = VWM_TICK_FREQ;

    timer_create(CLOCK_REALTIME, NULL, &clock_data->timer_id);
    timer_settime(clock_data->timer_id, 0, &clock_data->itimer, NULL);

    return clock_data;
}

pt_t
vwm_clock_driver(void * const env)
{
    clock_data_t        *clock_data;
    pt_context_t        *ctx_timer;

    extern unsigned int clock_tick;

    ctx_timer = (pt_context_t *)env;
    clock_data = (clock_data_t *)ctx_timer->anything;
	pt_resume(ctx_timer);

	do
	{
        if(clock_tick == 0)
		{
			pt_yield(ctx_timer);
            continue;
		}

        clock_tick = 0;
		viper_event_run(VIPER_EVENT_BROADCAST,"vwm-clock-tick");
	}
	while(!(*ctx_timer->shutdown));

    timer_delete(clock_data->timer_id);
    free(clock_data);

	return PT_DONE;
}

void
vwm_clock_driver_SIGALARM(int signum, siginfo_t *siginfo, void *uc)
{
    extern unsigned int     clock_tick;

    (void)siginfo;
    (void)uc;

    if (signum != SIGALRM) return;

    clock_tick = 1;
}
