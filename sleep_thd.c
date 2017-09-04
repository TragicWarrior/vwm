#include <time.h>

#include <protothread.h>

#include "vwm.h"
#include "private.h"
#include "sleep_thd.h"


pt_t vwm_sleep_driver(void * const env)
{
    pt_context_t        *ctx_sleep;
    sig_atomic_t        *wake_counter;
    struct timespec     interval = { .tv_sec = 0, .tv_nsec = 10000 };

    ctx_sleep = (pt_context_t *)env;
    wake_counter = (sig_atomic_t *)ctx_sleep->anything;

	pt_resume(ctx_sleep);

	do
	{
        if(*wake_counter > 0)
        {
            if(*wake_counter > 0) *wake_counter--;

            pt_yield(ctx_sleep);
            continue;
        }

        nanosleep(&interval, NULL);
        pt_yield(ctx_sleep);
	}
	while(!(*ctx_sleep->shutdown));

	return PT_DONE;
}
