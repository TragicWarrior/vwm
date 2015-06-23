#ifndef _H_VWM_PANEL_
#define _H_VWM_PANEL_

#include <inttypes.h>

#include <glib.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#define  VWM_PANEL_STATE_FROZEN  (1<<1)

/* maximum seconds a message can live on the panel before needing
   be touched. */
#define  VWM_PANEL_MSG_TTL_MAX   30       

typedef struct
{
   GSList   *msg_list;
   gint8    msg_count;
   gchar    *display;
   gint16   display_sz;
   gchar    *pos;
   gint32   tick_rate;
   gint32   freeze_time;
   gint32   thaw_timer;
   gint16   thaw_rate;
   gint32   clock;
   guint32  state;
}
VWM_PANEL;

typedef struct
{
   union
   {
      gpointer    msg_addr;
      uintmax_t   msg_handle;
   }
                                 msg_id;  
   gchar                         *msg;
   gint                          msg_len;
   gint32                        timeout;
   gint32                        touch_val;
}
VWM_PANEL_MSG;

/* panel events   */
gint     vwm_panel_ON_TERM_RESIZED(WINDOW *window,gpointer arg);
gint     vwm_panel_ON_CLOCK_TICK(WINDOW *window,gpointer arg);

/* helpers  */
void	   vwm_panel_update_throbber(WINDOW *window);
void     vwm_panel_update_clock(WINDOW *window);
void     vwm_panel_marshall(VWM_PANEL *vwm_panel);
void     vwm_panel_display(VWM_PANEL *vwm_panel,WINDOW *window);
void     vwm_panel_scroll(VWM_PANEL *vwm_panel);

#endif
