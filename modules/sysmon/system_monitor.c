/*************************************************************************
 * All portions of code are copyright by their respective author/s.
 * Copyright (C) 2007      Bryan Christ <bryan.christ@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *----------------------------------------------------------------------*/

#include "system_monitor.h"

#include <malloc.h>
#include <unistd.h>

#ifdef _GNU_SOURCE
#include <sys/sysinfo.h>
#endif

/*
#include <sys/times.h>
*/

#include <sys/time.h>
#include <sys/resource.h>

/*
#include <glibtop.h>
#include <glibtop/open.h>
#include <glibtop/cpu.h>
#include <glibtop/mem.h>
#include <glibtop/swap.h>
#include <glibtop/procmem.h>
*/

#include <viper.h>
#include <pseudo.h>

#include "../../vwm.h"

#define	SWIN_HEIGHT_LOAD	3
#define	SWIN_HEIGHT_PST	3
#define 	SWIN_HEIGHT_MEM	11

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module)
{
	gchar *libfilename;

	/*	preload libgtop for use with this module	*/
    /*
	if(g_module_open("libgtop-2.0.so",G_MODULE_BIND_LAZY)==NULL)
		return "\ncould not preload libgtop-2.0.so";
    */

	/*	register with vwm 	*/
	libfilename = (gchar*)g_module_name(module);
	vwm_module_add("Tools","System Monitor",system_monitor,NULL,libfilename);

	return NULL;
}

WINDOW*
system_monitor(void *anything)
{
    WINDOW      *window;
	WINDOW      **subwin;
	int			cpu_count = 0;
	gint		height;

#ifdef _VIPER_WIDE
	static cchar_t  fill_char;
	wchar_t			wch[] = {0x0020,0x0000};
#endif

	/*	prevent multiple reloads	*/
	if(viper_window_find_by_class((void*)system_monitor) != NULL)
		return NULL;

   /* how many processors are in the system  */
#ifdef _GNU_SOURCE
   cpu_count = get_nprocs();
#else
   cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
   /* every system has at least 1 CPU.  if the above methods fail, this is
      the fallback.  */
   if(cpu_count < 1) cpu_count = 1;

	height = SWIN_HEIGHT_MEM;
	if((cpu_count + SWIN_HEIGHT_LOAD + SWIN_HEIGHT_PST) > height)
        height = cpu_count + SWIN_HEIGHT_LOAD + SWIN_HEIGHT_PST;

	viper_thread_enter();
	window = viper_window_create(" System Monitor ",
        0.5,0.5,50,height + 2,TRUE);
	viper_window_set_class(window,(gpointer)system_monitor);
	viper_window_show(window);

   /* allocate subwins for each sensor */
	subwin = (WINDOW**)g_malloc0(sizeof(WINDOW*)*SENSOR_COUNT);
	subwin[SENSOR_LOAD] = derwin(window,SWIN_HEIGHT_LOAD,23,1,1);
	subwin[SENSOR_PST] = derwin(window,SWIN_HEIGHT_PST,23,4,1);
	subwin[SENSOR_CPU] = derwin(window,cpu_count,23,7,1);
	subwin[SENSOR_MEM] = derwin(window,SWIN_HEIGHT_MEM,23,1,26);

#ifdef _VIPER_WIDE
	setcchar(&fill_char,wch,0,0,NULL);
	window_fill(window,&fill_char,viper_color_pair(COLOR_BLACK,COLOR_WHITE),
      A_NORMAL);
	mvwvline_set(window,0,24,WACS_VLINE,height+2);
#else
	window_fill(window,' ',viper_color_pair(COLOR_BLACK,COLOR_WHITE),A_NORMAL);
	mvwvline(window,0,24,ACS_VLINE,height+2);
#endif

	viper_event_set(window,"window-destroy",system_monitor_ON_DESTROY,
		(gpointer)subwin);
    /* drive the sensor off the common clock provided by VWM */
    viper_event_set(window,"vwm-clock-tick",system_monitor_ON_CLOCK_TICK,
        (gpointer)subwin);

	viper_thread_leave();

	return window;
}

int
system_monitor_ON_CLOCK_TICK(WINDOW *window,void *anything)
{
    WINDOW   **subwins;

    subwins = (WINDOW**)anything;

    system_monitor_load(subwins[SENSOR_LOAD]);
    system_monitor_pst(subwins[SENSOR_PST]);
    system_monitor_cpu(subwins[SENSOR_CPU]);
    system_monitor_mem(subwins[SENSOR_MEM]);

    viper_thread_enter();
    viper_window_redraw(window);
    viper_thread_leave();

    return 0;
}

int
system_monitor_load(WINDOW *subwin)
{
	static unsigned int     tick = 0;
    double                  loads[3];

    tick++;

    // exec only on every 5th tick
    if((tick % 5) != 0) return 0;

    /* getloadavg() is not as portable as the similar libgtop2 function but
        it is supported by Solaris and the BSD variants   */
    getloadavg(loads,3);

	viper_thread_enter();
	mvwprintw(subwin,0,0,"System Load");
	mvwprintw(subwin,1,0,"%.02f %.02f %.02f",loads[0],loads[1],loads[2]);
	viper_thread_leave();

    // reset tick counter to prevent overflow
    tick = 0;

	return 1;
}


int
system_monitor_pst(WINDOW *subwin)
{
    int     ui_speed;

    ui_speed =  vwm_ui_get_speed();

	viper_thread_enter();
	mvwprintw(subwin,0,0,"UI Tasks");
	mvwprintw(subwin,1,0,"%d",ui_speed());
	wclrtoeol(subwin);
	viper_thread_leave();

	return 1;
}

/*
int
system_monitor_cpu(WINDOW *subwin)
{
   static glibtop_cpu	cpu_new;
	static glibtop_cpu	cpu_old;
	static gint			   cpu_count=0;
	static gdouble		   *percent_used;
	gdouble				   percent_idle;
	guint64				   elapsed_idle,elapsed_total;
   static gint32        tick=0;
	gint			   	   i;
	gchar				      label_l[20];
	gchar				      label_r[10];

   /* subwin==NULL is the clue that we're shutting down  */
   if(subwin==NULL)
   {
      g_free(percent_used);
      tick=0;
      return 0;
   }

   if(tick==0)
   {
#ifdef _GNU_SOURCE
      cpu_count=get_nprocs();
#else
      cpu_count=sysconf(_SC_NPROCESSORS_ONLN);
#endif
      /* every system has at least 1 CPU.  if the above methods fail, this is
      the fallback.  */
      if(cpu_count<1) cpu_count=1;

      percent_used=(gdouble*)g_malloc0(cpu_count*sizeof(gdouble));
      tick++;
      return 0;
   }

   tick++;
   if((tick%7)!=0) return 0;

	memcpy(&cpu_old,&cpu_new,sizeof(glibtop_cpu));
	glibtop_get_cpu(&cpu_new);

	viper_thread_enter();
	wmove(subwin,0,0);
	for(i=0;i<cpu_count;i++)
	{
		elapsed_total=cpu_new.xcpu_total[i]-cpu_old.xcpu_total[i];
		elapsed_idle=cpu_new.xcpu_idle[i]-cpu_old.xcpu_idle[i];
		percent_idle=(gdouble)elapsed_idle/(gdouble)elapsed_total;
		percent_used[i]=1-percent_idle;
		sprintf(label_l,"CPU %2d ",i);
		sprintf(label_r," %-5.1f",percent_used[i]*100);
		draw_curses_pbar(subwin,23,label_l,label_r,percent_used[i]);
	}

	viper_thread_leave();

	return 1;
}
*/

/*
int
system_monitor_mem(WINDOW *subwin)
{
    glibtop_mem				mem_stats;
	glibtop_swap			swap_stats;
	glibtop_proc_mem		proc_mem_stats;
 	struct mallinfo		malloc_stats;
    static gint32        tick=0;

   tick++;
   if((tick%7)!=0) return 0;

	/*	get statistics	*/
	glibtop_get_mem(&mem_stats);
	glibtop_get_swap(&swap_stats);
	glibtop_get_proc_mem(&proc_mem_stats,getpid());
	malloc_stats=mallinfo();

	viper_thread_enter();

	wmove(subwin,0,0);
	wprintw(subwin,"%-12s%8d MB",
		"[Memory]",(guint32)(mem_stats.total>>20));
	wprintw(subwin,"%-12s%8d MB",
		"..free",(guint32)(mem_stats.free>>20));
	wprintw(subwin,"%-12s%8d MB",
		"..cached",(guint32)(mem_stats.cached>>20));
	wprintw(subwin,"%-12s%8d MB",
		"..buffers",(guint32)(mem_stats.buffer>>20));
	wprintw(subwin,"\n\r%-12s%8d MB",
		"[Swap]",(guint32)(swap_stats.total>>20));
	wprintw(subwin,"%-12s%8d MB",
		"..free",(guint32)(swap_stats.free>>20));
	wprintw(subwin,"\n\r%-12s%8d KB",
		"[VWM]",(guint32)(proc_mem_stats.size>>10));
	wprintw(subwin,"%-12s%8d KB",
		"..resident",(guint32)(proc_mem_stats.resident>>10));
	wprintw(subwin,"%-12s%8d KB",
		"..dynamic",(guint32)(malloc_stats.uordblks>>10));

	viper_thread_leave();

	return 1;
}
*/

int
system_monitor_ON_DESTROY(WINDOW *window,gpointer anything)
{
    WINDOW  **subwins;
    int     i;

	subwins = (WINDOW**)anything;

    // system_monitor_cpu(NULL);
    for(i = 0; i < SENSOR_COUNT;i++) delwin(subwins[i]);

	g_free(subwins);

	return 0;
}

/*
void draw_curses_pbar(WINDOW *window,gint width,gchar *label_l,gchar *label_r,
	gfloat value)
{
	gint		w_height,w_width;
	gint		label_l_sz,label_r_sz;
	gint		meter_len;
	gint		color;
	gfloat	pos;
	gint		i=0;

	getmaxyx(window,w_height,w_width);
	if(w_width<width || width==-1) width=w_width;

	label_l_sz=strlen(label_l);
	label_r_sz=strlen(label_r);
	meter_len=width-label_l_sz-label_r_sz;
	if(meter_len<3) return;

	wattrset(window,A_NORMAL);
	wprintw(window,label_l);

	for(i=0;i<meter_len;i++)
	{
		pos=(gfloat)i/(gfloat)meter_len;
		color=VIPER_COLORS(COLOR_BLACK,COLOR_GREEN);
		if(pos>0.40) color=VIPER_COLORS(COLOR_BLACK,COLOR_YELLOW);
		if(pos>0.80) color=VIPER_COLORS(COLOR_BLACK,COLOR_RED);
		if(pos>value) color=VIPER_COLORS(COLOR_BLACK,COLOR_BLACK);
		wattron(window,color | A_UNDERLINE);
		wprintw(window,"_");
		wattrset(window,A_NORMAL);
	}

	wprintw(window,label_r);

	return;
}
*/
