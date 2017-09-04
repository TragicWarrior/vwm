#include <time.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <protothread.h>

#include "vwm.h"
#include "clock_thd.h"
#include "private.h"


pt_t vwm_clock_driver(void * const env)
{
    clock_data_t        *clock_data;
    pt_context_t        *ctx_timer;

    ctx_timer = (pt_context_t *)env;
    clock_data = (clock_data_t*)ctx_timer->anything;
	pt_resume(ctx_timer);

	do
	{
		clock_data->elapsed = g_timer_elapsed(clock_data->timer, NULL);
   		if(clock_data->elapsed < VWM_CLOCK_TICK)
		{
			pt_yield(ctx_timer);
            continue;
		}

		viper_event_run(VIPER_EVENT_BROADCAST,"vwm-clock-tick");
		g_timer_start(clock_data->timer);
	}
	while(!(*ctx_timer->shutdown));

    g_timer_destroy(clock_data->timer);
    free(clock_data);

	return PT_DONE;
}
