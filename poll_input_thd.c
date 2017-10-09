#include <time.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <protothread.h>

#include "vwm.h"
#include "poll_input_thd.h"
#include "private.h"

pt_t
vwm_poll_input(void * const env)
{
    int32_t         keystroke;
    static MEVENT   mouse_event;
    pt_context_t    *ctx_poll_input;

    ctx_poll_input = (pt_context_t *)env;
	pt_resume(ctx_poll_input);

    do
    {
        keystroke = viper_kmio_fetch(&mouse_event);

        if(keystroke != -1)
        {
            viper_kmio_dispatch(keystroke,&mouse_event);
        }

        pt_yield(ctx_poll_input);
    }
    while(!(*ctx_poll_input->shutdown));

    return PT_DONE;
}
