#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "clock.h"
#include "private.h"

pt_t
vwm_clock_driver(void * const env)
{
    vwm_sched_ctx_t     *ctx_timer;

    extern unsigned int clock_tick;

    ctx_timer = (vwm_sched_ctx_t *)env;
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
        ctx_timer->did_work = 1;
        pt_yield(ctx_timer);
	}
	while(!(*ctx_timer->shutdown));

	return PT_DONE;
}
