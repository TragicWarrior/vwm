#ifndef _H_SYSTEM_MONITOR_
#define _H_SYSTEM_MONITOR_

#include <glib.h>
#include <gmodule.h>
#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

enum {
   SENSOR_LOAD=0x0,
   SENSOR_PST,
   SENSOR_CPU,
   SENSOR_MEM,
   SENSOR_COUNT   /* not an index but a way to reference the maximum number of
                     enumerated sensors.  */
};

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module);

WINDOW*	system_monitor(gpointer anything);

/*	event handlers	*/
/* gint	system_monitor_ON_CLOSE(WINDOW *window,gpointer anything); */
gint	   system_monitor_ON_DESTROY(WINDOW *window,gpointer anything);
gint     system_monitor_ON_CLOCK_TICK(WINDOW *window,gpointer anything);

/*	sensor functions	*/
gint	   system_monitor_load(WINDOW *subwin);
gint	   system_monitor_pst(WINDOW *subwin);
gint	   system_monitor_cpu(WINDOW *subwin);
gint	   system_monitor_mem(WINDOW *subwin);

/*	helper functions	*/
void   draw_curses_pbar(WINDOW *window,gint width,
		   gchar *label_l,gchar *label_r,gfloat value);

#endif
