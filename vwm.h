#ifndef _H_VWM_
#define _H_VWM_

#include <inttypes.h>

#include <sys/types.h>

#include <protothread.h>

#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

#include <viper.h>

#define VWM_VERSION					"3.0.0"

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

typedef struct _vwm_s           vwm_t;
typedef struct _vwm_module_s    vwm_module_t;
typedef struct _vwm_profile_s   vwm_profile_t;


/*	startup functions	*/
vwm_t*          vwm_init(void);
#define			vwm_get_instance()	            (vwm_init())
void 			vwm_hook_enter(ViperFunc func, void *arg);
void			vwm_hook_leave(ViperFunc func, void *arg);

/* panel facilities  */
vwnd_t*         vwm_panel_init(void);
#define         vwm_panel_get_instance()         (vwm_panel_init())
int16_t         vwm_panel_ctrl(uint32_t ctrl, int16_t val);
#define         vwm_panel_freeze_set(timeout) \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE, timeout))
#define         vwm_panel_freeze_get() \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE, -1))
#define         vwm_panel_freeze_now() \
                    (vwm_panel_ctrl(VWM_PANEL_FREEZE, 0))
#define         vwm_panel_thaw_now() \
                    (vwm_panel_ctrl(VWM_PANEL_THAW, 0))
#define         vwm_panel_clear() \
                    (vwm_panel_ctrl(VWM_PANEL_CLEAR,0))
uintmax_t       vwm_panel_message_add(char *msg, int timeout);
void            vwm_panel_message_del(uintmax_t msg_id);
uintmax_t       vwm_panel_message_find(char *msg);
int             vwm_panel_message_touch(uintmax_t msg_id);
int             vwm_panel_message_promote(uintmax_t msg_id);

/*	extensibility functions	*/
vwm_module_t*   vwm_module_create(void);
void            vwm_module_set_type(vwm_module_t *mod, int type);
int             vwm_module_get_type(vwm_module_t *mod);
void            vwm_module_set_title(vwm_module_t *mod, char *title);
void            vwm_module_get_title(vwm_module_t *mod, char *buf, int buf_sz);
void            vwm_module_set_userptr(vwm_module_t *mod, void *anything);
void*           vwm_module_get_userptr(vwm_module_t *mod);
int 		    vwm_module_add(const vwm_module_t *mod);
vwnd_t*         vwm_module_exec(vwm_module_t *mod);

vwm_module_t*   vwm_module_find_by_title(char *title);
vwm_module_t*   vwm_module_find_by_type(vwm_module_t *prev, int type);

char*	        vwm_modules_load(char *module_dir);

/* profile functions */
int             vwm_profile_init(vwm_t *vwm);
char*           vwm_profile_mod_dir_get(vwm_t *vwm);
void            vwm_profile_mod_dir_set(char *module_dir);
char*           vwm_profile_login_get(vwm_t *vwm);
char*           vwm_profile_rc_file_get(vwm_t *vwm);

/* screensaver functions   */
void            vwm_scrsaver_start(void);
void            vwm_scrsaver_stop(void);
void            vwm_scrsaver_set(char *title);
const char*     vwm_scrsaver_get(void);
void            vwm_scrsaver_timeout_set(int timeout);
int             vwm_scrsaver_timeout_get(void);

/*	helper functions	*/
void            vwm_post_help(char *msg);

/* helper macros  */


#endif
