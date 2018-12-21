#include <time.h>

#include <ncursesw/curses.h>

#include <protothread.h>

#include "vwm.h"
#include "poll_input_thd.h"
#include "private.h"

pt_t
vwm_poll_input(void * const env)
{
    int32_t         keystroke;
    MEVENT          *mouse_event;
    // extern vwm_t    *vwm;

    pt_context_t    *ctx_poll_input;

    ctx_poll_input = (pt_context_t *)env;
    mouse_event = (MEVENT*)ctx_poll_input->anything;

	pt_resume(ctx_poll_input);

    do
    {
        keystroke = viper_kmio_fetch(mouse_event);

        if(keystroke != -1)
        {
            viper_kmio_dispatch(keystroke, mouse_event);
        }

        pt_yield(ctx_poll_input);
    }
    while(!(*ctx_poll_input->shutdown));

    return PT_DONE;
}
