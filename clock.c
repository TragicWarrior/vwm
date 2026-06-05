#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "clock.h"
#include "private.h"
#include "panel.h"

pt_t
vwm_clock_driver(void * const env)
{
    vwm_sched_ctx_t     *ctx_timer;
    vwm_t               *vwm;

    extern unsigned int clock_tick;

    ctx_timer = (vwm_sched_ctx_t *)env;
    vwm = vwm_get_instance();
	pt_resume(ctx_timer);

	do
	{
        if(clock_tick == 0)
		{
			pt_yield(ctx_timer);
            continue;
		}

        clock_tick = 0;
        vwm_panel_ON_CLOCK_TICK(vwm_panel_get_data());
        vk_screen_refresh(vwm->screen);
        ctx_timer->did_work = 1;
        pt_yield(ctx_timer);
	}
	while(!(*ctx_timer->shutdown));

	return PT_DONE;
}
