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

#include <string.h>
#include <time.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <viper.h>

#include "vwm.h"
#include "vwm_panel.h"
#include "strings.h"


WINDOW*
vwm_panel_init(void)
{
    static WINDOW   *window = NULL;
    VWM_PANEL       *vwm_panel;
    int             max_y, max_x;

    if(window != NULL) return window;

    vwm_panel = (VWM_PANEL*)calloc(1, sizeof(VWM_PANEL));
    vwm_panel->tick_rate = 2;
    vwm_panel->thaw_rate = 3;

	// viper_thread_enter();
	window = viper_window_create("vwm panel", 0, 0, WSIZE_FULLSCREEN, 1, FALSE);
	wbkgdset(window, VIPER_COLORS(COLOR_BLACK, COLOR_WHITE));
    viper_event_set(window, "vwm-clock-tick", vwm_panel_ON_CLOCK_TICK,
        (void*)vwm_panel);
    viper_event_set(window,"term-resized", vwm_panel_ON_TERM_RESIZED,
        (void*)vwm_panel);
    viper_window_set_userptr(window, (void*)vwm_panel);

    getmaxyx(window, max_y, max_x);
    if(max_x > 25) vwm_panel->display = (char*)calloc(1, max_x - 21);
    vwm_panel->display_sz = max_x - 22;

    wattron(window, VIPER_COLORS(COLOR_BLACK, COLOR_WHITE));
    mvwprintw(window, 0, 0, "%*c", vwm_panel->display_sz, ' ');

    INIT_LIST_HEAD(&vwm_panel->msg_list);

	viper_window_redraw(window);

    // squelch compiler warning
    (void)max_y;

	return window;
}

int
vwm_panel_ON_TERM_RESIZED(WINDOW *window, void *arg)
{
    extern WINDOW   *SCREEN_WINDOW;
    VWM_PANEL       *vwm_panel;
    int             max_y, max_x;

    vwm_panel = (VWM_PANEL*)arg;
    getmaxyx(SCREEN_WINDOW, max_y, max_x);

    viper_wresize_abs(window, WSIZE_FULLSCREEN, WSIZE_UNCHANGED);
    werase(window);

    /* resize the marquee based on new metrics.  */
    if(max_x < 25)
    {
        if(vwm_panel->display != NULL) free(vwm_panel->display);
        vwm_panel->display_sz = max_x - 22;
        vwm_panel->display = NULL;
    }
    else
    {
        vwm_panel->display = (char*)realloc(vwm_panel->display, max_x - 21);
        vwm_panel->display_sz = max_x - 22;
    }

    viper_window_redraw(window);

    // squelch compiler warnings
    (void)max_y;

    return 0;
}

int
vwm_panel_ON_CLOCK_TICK(WINDOW *window, void *arg)
{
    VWM_PANEL   *vwm_panel;

    vwm_panel = (VWM_PANEL*)arg;
    vwm_panel->clock++;

    /* update throbber on every tick (currently 1/10 sec). */
    vwm_panel_update_throbber(window);

    /* update clock and marshall the panel once every second.  */
    if((vwm_panel->clock % VWM_CLOCK_TICKS_PER_SEC) == 0)
    {
        vwm_panel_update_clock(window);
        /*
            vwm_panel_marshall() checks for expired messages and purges them.
            it also unfreezes the panel when the thaw_timer reaches 0.
        */
        // vwm_panel_marshall(vwm_panel);
    }

    /* scroll the panel messages */
    // if((vwm_panel->clock % vwm_panel->tick_rate) == 0)
    // {
    //     vwm_panel_scroll(vwm_panel);
    //     vwm_panel_display(vwm_panel, window);
    // }

    viper_window_redraw(window);

    return 0;
}

void
vwm_panel_update_throbber(WINDOW *window)
{
    int             throbber[] = {'-', '\\', '|', '/'};
	static uint8_t	position = 0;
    int             x, y;

	position++;
	position = position % 4;
    getmaxyx(window, y, x);

    wattron(window, VIPER_COLORS(COLOR_BLACK,COLOR_CYAN));
    mvwaddch(window, 0, x - 1, throbber[position]);

	return;
}

void
vwm_panel_update_clock(WINDOW *window)
{
	time_t			clock;
	struct tm		*local_time;
    int             x, y;

	clock = time(NULL);
	local_time = localtime((time_t*)&clock);

    getmaxyx(window, y, x);

    wattron(window, VIPER_COLORS(COLOR_BLACK,COLOR_CYAN));
    mvwprintw(window, 0, x - 22," %02d/%02d/%04d %02d:%02d:%02d ",
		local_time->tm_mon + 1, local_time->tm_mday, local_time->tm_year + 1900,
		local_time->tm_hour, local_time->tm_min, local_time->tm_sec);

    // squelch compiler warnings
    (void)y;

	return;
}

//void
//vwm_panel_marshall(VWM_PANEL *vwm_panel)
//{
//   VWM_PANEL_MSG  *vwm_panel_msg;
//   GSList         *node;

   /* decrement the thaw timer and unfreeze when timer expires.   */
//   if(vwm_panel->thaw_timer>0) vwm_panel->thaw_timer--;
//   if(vwm_panel->thaw_timer==0) vwm_panel->state &= ~VWM_PANEL_STATE_FROZEN;

   /* first, purge all expired messages.  */
//   node=vwm_panel->msg_list;
//   while(node!=NULL)
//   {
//      vwm_panel_msg=(VWM_PANEL_MSG*)node->data;
//     if(vwm_panel_msg->timeout==0)
//      {
//         vwm_panel_message_del(vwm_panel_msg->msg_id.msg_handle);
//         vwm_panel_marshall(vwm_panel);
//         break;
//      }
//      node=node->next;
//   }

   /* next, decrment all message timers.  */
//   node=vwm_panel->msg_list;
//   while(node!=NULL)
//   {
//      vwm_panel_msg=(VWM_PANEL_MSG*)node->data;
//      if(vwm_panel_msg->timeout!=-1) vwm_panel_msg->timeout--;
//      node=node->next;
//   }

//   return;
//}

void
vwm_panel_display(VWM_PANEL *vwm_panel, WINDOW *window)
{
   VWM_PANEL_MSG        *vwm_panel_msg;
    char                *pos;
   int                  i;

    if(list_empty(&vwm_panel->msg_list) || vwm_panel->display == NULL)
    {
        if(vwm_panel->display_sz > 0)
        {
            wattron(window, VIPER_COLORS(COLOR_BLACK, COLOR_WHITE));
            mvwprintw(window, 0, 0, "%*c", vwm_panel->display_sz, ' ');
            return;
        }
    }

    /* cycle to the next message in the list if necessary.  */
    // vwm_panel_scroll(vwm_panel);

    /* fill in the display buffer.   */
/*
    pos = vwm_panel->pos;
   for(i=0;i<vwm_panel->display_sz;i++)
   {
      if(*pos=='\0')
      {
         node=node->next;
         if(node==NULL)
         {
            if(vwm_panel_msg->msg_len <= vwm_panel->display_sz)
            {
               vwm_panel->display[i-4]='\0';
               break;
            }
            node=vwm_panel->msg_list;
         }
         vwm_panel_msg=(VWM_PANEL_MSG*)node->data;
         pos=&vwm_panel_msg->msg[0];
      }
      
      vwm_panel->display[i]=*pos;
      pos++;
   }
   vwm_panel->display[i]='\0';
*/

   /* write to panel.  */
   wattroff(window,VIPER_COLORS(COLOR_BLACK,COLOR_WHITE));
   mvwprintw(window,0,0,"%s",vwm_panel->display);
   i=vwm_panel->display_sz-strlen(vwm_panel->display);
   if(i>0) wprintw(window,"%*c",i,' ');
 
}

void
vwm_panel_scroll(VWM_PANEL *vwm_panel)
{
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    /*
        don't do anything if there are no messages to display or the panel
        is frozen.
    */
    if(list_empty(&vwm_panel->msg_list))
    {
        vwm_panel->pos = NULL;
        return;
    }

    /* do nothing if panel is frozen.   */
    if(vwm_panel->state & VWM_PANEL_STATE_FROZEN) return;

    /*
        this is a special case where there is only one message in the queue
        and it is short enough to fit on the display. */
    if((vwm_panel->msg_count == 1) &&
        (vwm_panel_msg->msg_len <= vwm_panel->display_sz))
    {
        vwm_panel->pos = vwm_panel_msg->msg;
        return;
    }

    // rotate the list and point the next entry
    list_rotate_left(&vwm_panel->msg_list);

    vwm_panel_msg = list_first_entry(&vwm_panel->msg_list, VWM_PANEL_MSG, list);
    vwm_panel->pos = vwm_panel_msg->msg;
    vwm_panel->thaw_timer = vwm_panel->thaw_rate;
    vwm_panel->state |= VWM_PANEL_STATE_FROZEN;

    return;
}

uintmax_t
vwm_panel_message_add(char *msg, int timeout)
{
    WINDOW          *window;
    VWM_PANEL       *vwm_panel;
    VWM_PANEL_MSG   *vwm_panel_msg;

    if(msg == NULL) return 0;

    window = vwm_panel_get_instance();
    vwm_panel = viper_window_get_userptr(window);

    if(timeout == 0 || timeout > VWM_PANEL_MSG_TTL_MAX)
        timeout = VWM_PANEL_MSG_TTL_MAX;

    /* create new panel message object. */
    vwm_panel_msg = (VWM_PANEL_MSG*)calloc(1, sizeof(VWM_PANEL_MSG));
    vwm_panel_msg->msg = strdup_printf("%s // ",msg);
    vwm_panel_msg->msg_len = strlen(msg);
    vwm_panel_msg->timeout = timeout;
    vwm_panel_msg->touch_val = timeout;
    vwm_panel_msg->msg_id.msg_addr = vwm_panel_msg;

    list_add(&vwm_panel_msg->list, &vwm_panel->msg_list);
    vwm_panel->msg_count++;

    /* always start the panel frozen.   */
    if(vwm_panel->msg_count == 1)
    {
        vwm_panel->state |= VWM_PANEL_STATE_FROZEN;
        vwm_panel->thaw_timer = vwm_panel->thaw_rate;
    }

    return vwm_panel_msg->msg_id.msg_handle;
}

void
vwm_panel_message_del(uintmax_t msg_id)
{
    WINDOW              *window;
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return;

    window = vwm_panel_get_instance();
    vwm_panel = viper_window_get_userptr(window);

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        list_del(pos);

        free(vwm_panel_msg->msg);
        free(vwm_panel_msg);

        vwm_panel->msg_count--;

    }

    return;
}

int
vwm_panel_message_touch(uintmax_t msg_id)
{
    WINDOW              *window;
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return -1;

    window = vwm_panel_get_instance();
    vwm_panel = viper_window_get_userptr(window);

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        vwm_panel_msg->timeout = vwm_panel_msg->touch_val;
        return (int)vwm_panel_msg->timeout;
    }

    return -1;
}

int
vwm_panel_message_promote(uintmax_t msg_id)
{
    WINDOW              *window;
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg_id == 0) return -1;

    window = vwm_panel_get_instance();
    vwm_panel = viper_window_get_userptr(window);

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(vwm_panel_msg->msg_id.msg_handle == msg_id) break;

        vwm_panel_msg = NULL;
    }

    if(vwm_panel_msg != NULL)
    {
        list_move(pos, &vwm_panel->msg_list);

        vwm_panel->thaw_timer = vwm_panel->thaw_rate;
        vwm_panel->pos = vwm_panel_msg->msg;
    }

    return 0;
}

uintmax_t
vwm_panel_message_find(char *msg)
{
    WINDOW              *window;
    VWM_PANEL           *vwm_panel;
    VWM_PANEL_MSG       *vwm_panel_msg;
    struct list_head    *pos;

    if(msg == NULL) return 0;

    window = vwm_panel_get_instance();
    vwm_panel = viper_window_get_userptr(window);

    list_for_each(pos, &vwm_panel->msg_list)
    {
        vwm_panel_msg = list_entry(pos, VWM_PANEL_MSG, list);

        if(strncmp(vwm_panel_msg->msg, msg, vwm_panel_msg->msg_len) == 0) break;

        vwm_panel_msg = NULL;
    }

    return vwm_panel_msg->msg_id.msg_handle;
}


