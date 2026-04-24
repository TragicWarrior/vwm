#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "poll_input_thd.h"
#include "private.h"

pt_t
vwm_poll_input(void * const env)
{
    int32_t             keystroke;
    MEVENT              *mouse_event;

    vwm_sched_ctx_t     *ctx_poll_input;

    ctx_poll_input = (vwm_sched_ctx_t *)env;
    mouse_event = (MEVENT*)ctx_poll_input->anything;

	pt_resume(ctx_poll_input);

    do
    {
        keystroke = viper_kmio_fetch(mouse_event);

        if(keystroke != -1)
        {
            viper_kmio_dispatch(keystroke, mouse_event);
            ctx_poll_input->did_work = 1;
        }

        pt_yield(ctx_poll_input);
    }
    while(!(*ctx_poll_input->shutdown));

    return PT_DONE;
}
