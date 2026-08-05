#ifndef _H_VWM_
#define _H_VWM_

#include <inttypes.h>

#include <sys/types.h>

#include <ncursesw/curses.h>

#include <vdk.h>
#include <vkmio.h>


#define VWM_VERSION					"4.9.7"

/* the kmio feature set vwm arms at startup and must re-arm whenever the
   outer terminal may have changed under us -- teleport to a new PTY, a
   terminal resize, or a dtach/abduco reattach (all of which arrive as
   KEY_RESIZE).  Keep the three call sites in lockstep through this one
   definition. */
#define VWM_KMIO_FLAGS \
    (VK_KMIO_MOUSE | VK_KMIO_MOUSE_HOVER | VK_KMIO_GPM_SIGIO)

#ifndef _VWM_SCREENSAVER_TIMEOUT
#define _VWM_SCREENSAVER_TIMEOUT    5
#endif

#ifndef _VWM_SHARED_MODULES
#define _VWM_SHARED_MODULES         "/usr/local/lib/vwm/"
#endif

#define VWM_CLOCK_TICK              (0.1F)
#define VWM_CLOCK_TICKS_PER_SEC     ((short int)(1/VWM_CLOCK_TICK))

#define VWM_STATE_NORMAL            0
#define VMW_STATE_ASLEEP            (1 << 1)    // screensaver active
#define VWM_STATE_ACTIVE            (1 << 2)    // indiates WM mode

/* VWM-specific event types */
#define VWM_EVENT_ON_CLOSE          100

/* desktop wallpaper patterns (per-surface, picked in Settings) */
#define VWM_WALLPAPER_NONE          0
#define VWM_WALLPAPER_STIPLE        1
#define VWM_WALLPAPER_SMALL_BRICKS  2
#define VWM_WALLPAPER_LARGE_BRICKS  3
#define VWM_WALLPAPER_DOTS_1        4
#define VWM_WALLPAPER_DOTS_2        5
#define VWM_WALLPAPER_CROSSES       6
#define VWM_WALLPAPER_COUNT         7

/* host clipboard sync mode (picked in Settings).  Controls whether a
   SELECT-mode copy is also pushed to the host clipboard, and by what
   transport. */
#define VWM_CLIPBOARD_NEVER         0
#define VWM_CLIPBOARD_OSC52         1
#define VWM_CLIPBOARD_XCLIP         2
#define VWM_CLIPBOARD_BOTH          3
#define VWM_CLIPBOARD_COUNT         4

extern const char *vwm_wallpaper_names[VWM_WALLPAPER_COUNT];
extern const char *vwm_color_names[16];
extern const char *vwm_clipboard_mode_names[VWM_CLIPBOARD_COUNT];

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

/* (re)arm the kmio/ncurses input state against the current tty.  Used
   at startup and re-run whenever the terminal may have changed under us
   -- teleport (new fd) and dtach reattach (new outer tty, possibly
   post-`reset`), both of which arrive as KEY_RESIZE. */
void            vwm_input_rearm(vwm_t *vwm);

void            vwm_apply_surface_count(int new_count);

/* panel facilities  */
void            vwm_panel_init(vwm_t *vwm);
uintmax_t       vwm_panel_message_add(char *msg, int timeout);
void            vwm_panel_message_del(uintmax_t msg_id);
uintmax_t       vwm_panel_message_find(char *msg);
int             vwm_panel_message_touch(uintmax_t msg_id);
int             vwm_panel_message_promote(uintmax_t msg_id);

/*	extensibility functions	*/
vwm_module_t*   vwm_module_create(void);
vwm_module_t*   vwm_module_clone(vwm_module_t *mod);
int             vwm_module_configure(vwm_module_t *mod, ...);
int             vwm_module_set_name(vwm_module_t *mod, char *name);
void            vwm_module_set_type(vwm_module_t *mod, int type);
int             vwm_module_get_type(vwm_module_t *mod);
int             vwm_module_get_zone(vwm_module_t *mod);
void            vwm_module_set_title(vwm_module_t *mod, char *title);
void            vwm_module_get_title(vwm_module_t *mod, char *buf, int buf_sz);
void            vwm_module_set_userptr(vwm_module_t *mod, void *anything);
void*           vwm_module_get_userptr(vwm_module_t *mod);
int 		    vwm_module_add(vwm_module_t *mod);
vk_window_t*    vwm_module_exec(vwm_module_t *mod);

int             vwm_module_type_value(char *string);
const char*     vwm_module_type_string(int value);
vwm_module_t*   vwm_module_find_by_name(char *name);
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

/* big-font hostname hook.  The optional vwmfont module registers a
   renderer on load (vwm_mod_init); the bottom-left hostname label uses it
   when its font size is not "Basic".  All NULL until a module registers,
   in which case the hostname falls back to the plain one-row label. */
typedef vk_widget_t* (*vwm_font_render_fn)(const char *text, int size,
                        int fill);
typedef void         (*vwm_font_apply_fn)(vk_widget_t *widget, short fg,
                        short bg);
typedef void         (*vwm_font_free_fn)(vk_widget_t *widget);

void                vwm_register_font_renderer(vwm_font_render_fn render,
                        vwm_font_apply_fn apply, vwm_font_free_fn free_fn);

/* helper macros  */


#endif
