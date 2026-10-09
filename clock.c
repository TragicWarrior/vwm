#include <sys/ioctl.h>

#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "clock.h"
#include "poll_input_thd.h"
#include "private.h"
#include "panel.h"
#include "screensaver.h"
#include "signals.h"
#include "winman.h"

/*
    Follow the terminal's size.  The session is not in the terminal's
    own session, so no SIGWINCH reaches it when the terminal is resized:
    compare TIOCGWINSZ against the canvas once per tick, and on a
    mismatch queue a synthetic KEY_RESIZE so the existing cascade in
    poll_input_thd reflows everything.
*/
static void
check_destination_resize(vwm_t *vwm)
{
    int             fd;
    struct winsize  ws;
    WINDOW         *canvas;
    int             cur_h;
    int             cur_w;

    fd = vk_screen_get_fd(vwm->screen);
    if(fd < 0) return;

    if(ioctl(fd, TIOCGWINSZ, &ws) != 0) return;
    if(ws.ws_row == 0 || ws.ws_col == 0) return;

    canvas = vk_screen_get_window(vwm->screen);
    if(canvas == NULL) return;

    getmaxyx(canvas, cur_h, cur_w);

    /* geometry drifted from the canvas */
    if((int)ws.ws_row != cur_h || (int)ws.ws_col != cur_w)
    {
        ungetch(KEY_RESIZE);
        vwm_input_wake();       /* the key is in ncurses, not on a fd */
    }
}

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
        /* woken, but no tick: the heartbeat also fires when a signal
           interrupts the scheduler's sleep.  only a real tick does the
           per-tick work below. */
        if(clock_tick == 0)
		{
			vwm_sched_wait(ctx_timer);
            continue;
		}

        clock_tick = 0;
        vwm_panel_ON_CLOCK_TICK(vwm_panel_get_data());
        vwm_screensaver_tick();
        check_destination_resize(vwm);
        if(vwm->attention != NULL)
        {
            vwm->attention_hold++;
            if(vwm->attention_hold >= VWM_ATTENTION_TICKS)
            {
                vwm->attention_hold = 0;
                vwm->attention_phase = !vwm->attention_phase;
                vk_window_update(VK_WINDOW(vwm->attention));
            }
        }
        vk_screen_refresh(vwm->screen);

        /* nothing more until the next tick */
        vwm_sched_wait(ctx_timer);
	}
	while(!(*ctx_timer->shutdown));

	return PT_DONE;
}
