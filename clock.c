#include <sys/ioctl.h>

#include <ncursesw/curses.h>

#include "protothread.h"

#include "vwm.h"
#include "clock.h"
#include "private.h"
#include "panel.h"
#include "screensaver.h"
#include "signals.h"

/*
    Two reattach/resize cases the poll loop can't see on its own, both
    resolved by queueing a synthetic KEY_RESIZE so the existing cascade in
    poll_input_thd reflows everything and resyncs terminal state:

    1. Geometry changed without a KEY_RESIZE.  SIGWINCH only fires for vwm's
       controlling TTY, which is the launch terminal -- after teleport the
       destination PTY can resize without vwm ever hearing about it.  Poll
       TIOCGWINSZ against the canvas and reflow on a mismatch.

    2. dtach reattach onto a same-size terminal.  dtach's `-r winch` sends
       SIGWINCH on attach, but ncurses only synthesizes KEY_RESIZE when the
       dimensions changed, so an unchanged-size reattach would never run the
       cascade -- the mouse stays dead, the cursor visible and keypad mode
       cleared until the user manually resizes.  vwm_SIGWINCH records the
       signal in vwm_winch_pending; force a KEY_RESIZE here even though the
       geometry matches.
*/
static void
check_destination_resize(vwm_t *vwm)
{
    int             fd;
    struct winsize  ws;
    WINDOW         *canvas;
    int             cur_h;
    int             cur_w;
    int             winch;

    /* consume the reattach flag every tick so it can't accumulate and
       fire a stray reflow on a later idle tick */
    winch = vwm_winch_pending;
    vwm_winch_pending = 0;

    fd = vk_screen_get_fd(vwm->screen);
    if(fd < 0) return;

    if(ioctl(fd, TIOCGWINSZ, &ws) != 0) return;
    if(ws.ws_row == 0 || ws.ws_col == 0) return;

    canvas = vk_screen_get_window(vwm->screen);
    if(canvas == NULL) return;

    getmaxyx(canvas, cur_h, cur_w);

    /* case 1: geometry drifted from the canvas */
    if((int)ws.ws_row != cur_h || (int)ws.ws_col != cur_w)
    {
        ungetch(KEY_RESIZE);
        return;
    }

    /* case 2: same size, but a SIGWINCH (reattach) just landed */
    if(winch)
        ungetch(KEY_RESIZE);
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
        if(clock_tick == 0)
		{
			pt_yield(ctx_timer);
            continue;
		}

        clock_tick = 0;
        vwm_panel_ON_CLOCK_TICK(vwm_panel_get_data());
        vwm_screensaver_tick();
        check_destination_resize(vwm);
        vk_screen_refresh(vwm->screen);
        ctx_timer->did_work = 1;
        pt_yield(ctx_timer);
	}
	while(!(*ctx_timer->shutdown));

	return PT_DONE;
}
