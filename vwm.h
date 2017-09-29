#ifndef _H_VWM_
#define _H_VWM_

#include <inttypes.h>

#include <glib.h>
#include <protothread.h>

#include <sys/types.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <viper.h>

#define VWM_VERSION					"2.8.0"

#ifndef _VWM_SCREENSAVER_TIMEOUT
#define _VWM_SCREENSAVER_TIMEOUT    5
#endif

#ifndef _VWM_SHARED_MODULES
#ifdef  _VIPER_WIDE
#define _VWM_SHARED_MODULES         "/usr/lib/vwm/modules_wide/"
#else
#define _VWM_SHARED_MODULES         "/usr/lib/vwm/modules/"
#endif
#endif

#define VWM_CLOCK_TICK              (0.1F)
#define VWM_CLOCK_TICKS_PER_SEC     ((short int)(1/VWM_CLOCK_TICK))

#define VWM_TICK_FREQ               100000000   // nanoseconds for system tick

#define VWM_STATE_NORMAL            0
#define VMW_STATE_ASLEEP            (1 << 1)    // screensaver active
#define VWM_STATE_ACTIVE            (1 << 2)    // indiates WM mode

enum
{
    VWM_PANEL_FREEZE    =   0x1,
    VWM_PANEL_THAW,
    VWM_PANEL_REWIND,
    VWM_PANEL_ADVANCE,
    VWM_PANEL_CLEAR
};

enum
{
    VWM_MSG_SHUTDOWN    =   0x1,
};

typedef struct  _vwm_module_s   vwm_module_t;

typedef struct
{
    vwm_module_t        *scrsaver_mod;
    int                 timeout;
    GTimer              *timer;
    guint32             state;
}
VWM_SCRSAVER;

typedef struct
{
	GSList			    *category_list;
	GSList			    *module_list;
	GSList			    *key_list;
    VIPER_FUNC          wallpaper_agent;
	VWM_SCRSAVER        screen_saver;
    guint32             state;
}
VWM;

typedef struct
{
   uid_t    user;
   gchar    *login;
   gchar    *passwd;
   gchar    *home;
   gchar    *rc_file;
   gchar    *mod_dir;
}
VWM_PROFILE;

enum
{
    PT_PRIORITY_NORMAL      =   0x00,
    PT_PRIORITY_HIGH        =   0x01
};

typedef struct
{
    pt_thread_t     pt_thread;
    pt_func_t       pt_func;

    // data shared by all protothreads
    int             *shutdown;
    void            *anything;
 }
pt_context_t;


/*	startup functions	*/
VWM*            vwm_init(void);
#define			vwm_get_instance()	            (vwm_init())
void 			vwm_hook_enter(VIPER_FUNC func,gpointer arg);
void			vwm_hook_leave(VIPER_FUNC func,gpointer arg);

/* panel facilities  */
WINDOW*         vwm_panel_init(void);
#define         vwm_panel_get_instance()         (vwm_panel_init())
gint16          vwm_panel_ctrl(guint32 ctrl,gint16 val);
#define         vwm_panel_freeze_set(timeout) \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE,timeout))
#define         vwm_panel_freeze_get() \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE,-1))
#define         vwm_panel_freeze_now() \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE,0))
#define         vwm_panel_thaw_now() \
                    (vwm_panel_ctrl(VWM_PANEL_THAW,0))
#define         vwm_panel_clear() \
                    (vwm_panel_ctrl(VWM_PANEL_CLEAR,0))
uintmax_t       vwm_panel_message_add(gchar *msg,gint timeout);
void            vwm_panel_message_del(uintmax_t msg_id);
uintmax_t       vwm_panel_message_find(gchar *msg);
gint            vwm_panel_message_touch(uintmax_t msg_id);
gint            vwm_panel_message_promote(uintmax_t msg_id);

/*	extensibility functions	*/
vwm_module_t*   vwm_module_create(void);
void            vwm_module_set_type(vwm_module_t *mod,int type);
int             vwm_module_get_type(vwm_module_t *mod);
void            vwm_module_set_title(vwm_module_t *mod,char *title);
void            vwm_module_get_title(vwm_module_t *mod,char *buf,int buf_sz);
void            vwm_module_set_userptr(vwm_module_t *mod,void *anything);
void*           vwm_module_get_userptr(vwm_module_t *mod);
int 		    vwm_module_add(const vwm_module_t *mod);
WINDOW*         vwm_module_exec(vwm_module_t *mod);

vwm_module_t*   vwm_module_find_by_title(char *title);
vwm_module_t*   vwm_module_find_by_type(vwm_module_t *prev,int type);

gchar*	        vwm_modules_load(char *module_dir);
GSList*         vwm_modules_list(int type);


/* profile functions */
VWM_PROFILE*    vwm_profile_init(void);
#define         vwm_profile_acquire()      vwm_profile_init()
gchar*          vwm_profile_mod_dir_get();
void            vwm_profile_mod_dir_set(gchar *module_dir);
#define         VWM_MOD_DIR                (vwm_profile_mod_dir_get())
gchar*          vwm_profile_login_get();
#define         VWM_LOGIN                  (vwm_profile_login_get())
gchar*          vwm_profile_rc_file_get();
#define         VWM_RC_FILE                (vwm_profile_rc_file_get())

/* screensaver functions   */
void            vwm_scrsaver_start(void);
void            vwm_scrsaver_stop(void);
void            vwm_scrsaver_set(gchar *title);
const gchar*    vwm_scrsaver_get(void);
void            vwm_scrsaver_timeout_set(gint timeout);
gint            vwm_scrsaver_timeout_get(void);

/*	helper functions	*/
void            vwm_post_help(gchar *msg);

/* helper macros  */


#endif
