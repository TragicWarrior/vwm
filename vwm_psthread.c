#include <time.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <pseudo.h>

#include "vwm.h"
#include "vwm_psthread.h"

int16_t vwm_clock_driver(ps_task_t *task,void *anything)
{
   static GTimer  *timer=NULL;
   gfloat         elapsed;

   if(timer==NULL)
   {
      timer=g_timer_new();
      return PSTHREAD_CONTINUE;
   }

   elapsed=g_timer_elapsed(timer,NULL);
   if(elapsed<VWM_CLOCK_TICK) return PSTHREAD_CONTINUE;

   viper_thread_enter();
   viper_event_run(VIPER_EVENT_BROADCAST,"vwm-clock-tick");
   viper_thread_leave();

   g_timer_start(timer);
   return PSTHREAD_CONTINUE;
}

int16_t vwm_poll_input(ps_task_t *task,void *anything)
{
	gint32	      keystroke;
   static MEVENT  mouse_event;

	keystroke=viper_kmio_fetch(&mouse_event);

   if(keystroke != -1)
   {
      viper_kmio_dispatch(keystroke,&mouse_event);
      // vwm_scrsaver_reset();
      return PSTHREAD_GROW;
   }

   return PSTHREAD_CONTINUE;
}
