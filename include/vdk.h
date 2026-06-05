#ifndef _VDK_H_
#define _VDK_H_

#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

#undef  NCURSES_OPAQUE
#define NCURSES_OPAQUE 0
#include <ncursesw/curses.h>

#define VDK_COLOR_COUNT             8

void            vdk_color_init(void);
short           vdk_color_pair(short fg, short bg);

#define VDK_COLORS(fg, bg)  (COLOR_PAIR(vdk_color_pair(fg, bg)))

/* size / position sentinels */
#define WSIZE_UNCHANGED             -2
#define WPOS_UNCHANGED              -1

/* widget state flags */
#define VK_STATE_VISIBLE            (1UL << 1)
#define VK_STATE_FROZEN             (1UL << 3)
#define VK_STATE_NORESIZE           (1UL << 7)
#define VK_STATE_EXPAND             (1UL << 8)

/* frame / border styles */
#define VK_BORDER_NONE               0
#define VK_BORDER_SINGLE             1
#define VK_BORDER_DOUBLE             2
#define VK_BORDER_ASCII              3
#define VK_BORDER_REVERSE            (1 << 4)

/* button relief styles (in addition to VK_BORDER_SINGLE / VK_BORDER_ASCII) */
#define VK_BUTTON_BASIC             4

/* activity indicator styles */
#define VK_ACTIVITY_SPINNER         0
#define VK_ACTIVITY_DOTS            1
#define VK_ACTIVITY_CIRCLES         2
#define VK_ACTIVITY_BAR             3

/* separator styles */
#define VK_SEPARATOR_BLANK          1
#define VK_SEPARATOR_SINGLE         2
#define VK_SEPARATOR_DOUBLE         3

/* text justification */
#define VK_JUSTIFY_LEFT             0
#define VK_JUSTIFY_RIGHT            1
#define VK_JUSTIFY_CENTER           2

/* box orientation */
#define VK_BOX_HORIZONTAL           0
#define VK_BOX_VERTICAL             1

/* scrollbar flags */
#define VK_SCROLLBAR_NONE           0
#define VK_SCROLLBAR_VERTICAL       (1 << 0)
#define VK_SCROLLBAR_HORIZONTAL     (1 << 1)
#define VK_SCROLLBAR_BOTH           (VK_SCROLLBAR_VERTICAL | VK_SCROLLBAR_HORIZONTAL)

/* marquee scroll direction */
#define VK_SCROLL_LEFT              0
#define VK_SCROLL_RIGHT             1
#define VK_SCROLL_LOOP              2

/* listbox flags */
#define VK_FLAG_ALLOW_WRAP          (1 << 1)
#define VK_FLAG_FULL_WIDTH          (1 << 2)

/* item flags */
#define VK_ITEM_CHECKED             (1 << 0)

/* selectbox modes */
#define VK_SELECTBOX_CHECKBOX       0
#define VK_SELECTBOX_RADIO          1

/* vector / direction */
#define VK_VECTOR_LEFT              1
#define VK_VECTOR_RIGHT             -1

/* deck widget position */
#define VK_DECK_TOP                 0
#define VK_DECK_BOTTOM              1

/* event types */
enum
{
    /* lifecycle */
    VK_EVENT_ON_RESIZE      = 1,
    VK_EVENT_ON_RECREATE    = 2,
    VK_EVENT_ON_TELEPORT    = 3,

    /* interaction */
    VK_EVENT_ON_CLICK       = 10,
    VK_EVENT_ON_SELECT      = 11,
    VK_EVENT_ON_UNSELECT    = 12,
    VK_EVENT_ON_ACTIVATE    = 13,
    VK_EVENT_ON_SUBMIT      = 14,
    VK_EVENT_ON_CLOSE       = 15,

    /* state */
    VK_EVENT_ON_FOCUS       = 20,
    VK_EVENT_ON_UNFOCUS     = 21,
    VK_EVENT_ON_SCROLL      = 22,

    /* surface */
    VK_EVENT_ON_SURFACE_CHANGE = 30,
};

/* keystroke definitions */
#ifndef KEY_TAB
#define KEY_TAB                     9
#endif
#ifndef KEY_CRLF
#define KEY_CRLF                    10
#endif

/* vk klass typedefs */
typedef struct  _vk_object_s        vk_object_t;
typedef struct  _vk_screen_s        vk_screen_t;
typedef struct  _vk_surface_s       vk_surface_t;
typedef struct  _vk_widget_s        vk_widget_t;
typedef struct  _vk_container_s     vk_container_t;
typedef struct  _vk_listbox_s       vk_listbox_t;
typedef struct  _vk_selectbox_s     vk_selectbox_t;
typedef struct  _vk_dropdown_s      vk_dropdown_t;
typedef struct  _vk_frame_s         vk_frame_t;
typedef struct  _vk_scroller_s      vk_scroller_t;
typedef struct  _vk_window_s        vk_window_t;
typedef struct  _vk_box_s           vk_box_t;
typedef struct  _vk_label_s         vk_label_t;
typedef struct  _vk_textbox_s       vk_textbox_t;
typedef struct  _vk_marquee_s       vk_marquee_t;
typedef struct  _vk_deck_s          vk_deck_t;
typedef struct  _vk_button_s        vk_button_t;
typedef struct  _vk_filler_s        vk_filler_t;
typedef struct  _vk_input_s         vk_input_t;
typedef struct  _vk_activity_s      vk_activity_t;
typedef struct  _vk_menubar_s       vk_menubar_t;
typedef struct  _vk_filedialog_s    vk_filedialog_t;
typedef struct  _vk_calendar_s      vk_calendar_t;
typedef struct  _vk_popup_s         vk_popup_t;

/* callback typedefs */
typedef int         (*VkEventFunc)(vk_object_t *object, int event,
                        void *data);
typedef int         (*VkWidgetFunc)(vk_widget_t *widget, void *anything);
typedef int         (*VkKmioFunc)(vk_object_t *object, int32_t keystroke);
typedef void        (*VkScrollInfoFunc)(vk_widget_t *child,
                        int *content_h, int *content_w,
                        int *scroll_y, int *scroll_x);
typedef void        (*VkSurfaceBkgdFunc)(vk_screen_t *screen,
                        int surface_id, WINDOW *canvas);
typedef void        (*VkWindowDecorateFunc)(vk_window_t *window,
                        WINDOW *canvas, void *data);

/* cast macros */
#define VK_OBJECT(x)            ((vk_object_t *)x)
#define VK_SCREEN(x)            ((vk_screen_t *)x)
#define VK_WIDGET(x)            ((vk_widget_t *)x)
#define VK_CONTAINER(x)         ((vk_container_t *)x)
#define VK_LISTBOX(x)           ((vk_listbox_t *)x)
#define VK_SELECTBOX(x)         ((vk_selectbox_t *)x)
#define VK_DROPDOWN(x)          ((vk_dropdown_t *)x)
#define VK_FRAME(x)             ((vk_frame_t *)x)
#define VK_SCROLLER(x)          ((vk_scroller_t *)x)
#define VK_WINDOW(x)            ((vk_window_t *)x)
#define VK_BOX(x)               ((vk_box_t *)x)
#define VK_LABEL(x)             ((vk_label_t *)x)
#define VK_TEXTBOX(x)           ((vk_textbox_t *)x)
#define VK_MARQUEE(x)           ((vk_marquee_t *)x)
#define VK_DECK(x)              ((vk_deck_t *)x)
#define VK_BUTTON(x)            ((vk_button_t *)x)
#define VK_FILLER(x)            ((vk_filler_t *)x)
#define VK_INPUT(x)             ((vk_input_t *)x)
#define VK_ACTIVITY(x)          ((vk_activity_t *)x)
#define VK_MENUBAR(x)           ((vk_menubar_t *)x)
#define VK_FILEDIALOG(x)        ((vk_filedialog_t *)x)
#define VK_CALENDAR(x)          ((vk_calendar_t *)x)
#define VK_POPUP(x)             ((vk_popup_t *)x)

/* vk_object */
const char*     vk_object_get_klass_name(vk_object_t *object);
int             vk_object_set_kmio(vk_object_t *object, VkKmioFunc func);
int             vk_object_push_keystroke(vk_object_t *object,
                    int32_t keystroke);
int             vk_object_register_event(vk_object_t *object,
                    int event, VkEventFunc func, void *data);
int             vk_object_unregister_event(vk_object_t *object,
                    int event, VkEventFunc func);
int             vk_object_emit(vk_object_t *object, int event);
int             vk_object_destroy(vk_object_t *object);

/* vk_screen */
vk_screen_t*    vk_screen_create(void);
int             vk_screen_add_surface(vk_screen_t *screen);
int             vk_screen_del_surface(vk_screen_t *screen, int id);
int             vk_screen_set_surface(vk_screen_t *screen, int id);
int             vk_screen_get_active_surface(vk_screen_t *screen);
int             vk_screen_get_surface_count(vk_screen_t *screen);
WINDOW*         vk_screen_get_window(vk_screen_t *screen);
int             vk_screen_attach_widget(vk_screen_t *screen,
                    int surface_id, vk_widget_t *widget);
int             vk_screen_detach_widget(vk_screen_t *screen,
                    int surface_id, vk_widget_t *widget);
int             vk_screen_resize(vk_screen_t *screen);
int             vk_screen_teleport(vk_screen_t *screen, const char *pty);
int             vk_screen_set_wallpaper(vk_screen_t *screen,
                    VkSurfaceBkgdFunc func);
int             vk_screen_set_overlay(vk_screen_t *screen,
                    VkSurfaceBkgdFunc func);
int             vk_screen_refresh(vk_screen_t *screen);
void            vk_screen_destroy(vk_screen_t *screen);

/* vk_widget */
vk_widget_t*    vk_widget_create(int width, int height);
int             vk_widget_set_surface(vk_widget_t *widget, WINDOW *window);
WINDOW*         vk_widget_get_surface(vk_widget_t *widget);
WINDOW*         vk_widget_get_canvas(vk_widget_t *widget);
void            vk_widget_set_colors(vk_widget_t *widget, int fg, int bg);
int             vk_widget_get_colors(vk_widget_t *widget,
                    short *fg, short *bg);
void            vk_widget_set_attrs(vk_widget_t *widget, attr_t attrs);
attr_t          vk_widget_get_attrs(vk_widget_t *widget);
void            vk_widget_set_relief_colors(vk_widget_t *widget,
                    short hi, short lo);
int             vk_widget_get_metrics(vk_widget_t *widget,
                    int *width, int *height);
int             vk_widget_get_position(vk_widget_t *widget,
                    int *x, int *y);
int             vk_widget_erase(vk_widget_t *widget);
int             vk_widget_resize(vk_widget_t *widget, int width, int height);
int             vk_widget_recreate(vk_widget_t *widget);
void            vk_widget_fill(vk_widget_t *widget, chtype ch);
int             vk_widget_draw(vk_widget_t *widget);
uint32_t        vk_widget_get_state(vk_widget_t *widget);
void            vk_widget_set_state(vk_widget_t *widget, uint32_t state);
#define         vk_widget_freeze(w) \
                    vk_widget_set_state(w, vk_widget_get_state(w) | VK_STATE_FROZEN)
#define         vk_widget_thaw(w) \
                    vk_widget_set_state(w, vk_widget_get_state(w) & ~VK_STATE_FROZEN)
#define         vk_widget_show(w) \
                    vk_widget_set_state(w, vk_widget_get_state(w) | VK_STATE_VISIBLE)
#define         vk_widget_hide(w) \
                    vk_widget_set_state(w, vk_widget_get_state(w) & ~VK_STATE_VISIBLE)
#define         vk_widget_is_visible(w) \
                    (vk_widget_get_state(w) & VK_STATE_VISIBLE)
#define         vk_widget_set_expand(w) \
                    vk_widget_set_state(w, vk_widget_get_state(w) | VK_STATE_EXPAND)
int             vk_widget_move(vk_widget_t *widget, int x, int y);
void            vk_widget_set_userptr(vk_widget_t *widget, void *ptr);
void*           vk_widget_get_userptr(vk_widget_t *widget);
void            vk_widget_destroy(vk_widget_t *widget);

/* vk_container */
vk_container_t* vk_container_create(int width, int height);
int             vk_container_add_widget(vk_container_t *container,
                    vk_widget_t *widget);
int             vk_container_remove_widget(vk_container_t *container,
                    vk_widget_t *widget);
int             vk_container_vacate(vk_container_t *container);
int             vk_container_destroy(vk_container_t *container);

/* vk_listbox */
vk_listbox_t*   vk_listbox_create(int width, int height);
int             vk_listbox_set_wrap(vk_listbox_t *listbox, bool allowed);
int             vk_listbox_set_title(vk_listbox_t *listbox, char *title);
int             vk_listbox_get_title(vk_listbox_t *listbox,
                    char *buf, int buf_sz);
int             vk_listbox_set_highlight(vk_listbox_t *listbox, int fg, int bg);
int             vk_listbox_set_highlight_attrs(vk_listbox_t *listbox,
                    attr_t attrs);
int             vk_listbox_add_item(vk_listbox_t *listbox,
                    char *item, VkWidgetFunc func, void *anything);
int             vk_listbox_set_item(vk_listbox_t *listbox, int idx,
                    char *item, VkWidgetFunc func, void *anything);
int             vk_listbox_remove_item(vk_listbox_t *listbox, int idx);
int             vk_listbox_get_item(vk_listbox_t *listbox, int idx,
                    char *buf, int buf_sz);
int             vk_listbox_get_item_count(vk_listbox_t *listbox);
int             vk_listbox_get_scroll_pos(vk_listbox_t *listbox);
int             vk_listbox_get_curr(vk_listbox_t *listbox);
int             vk_listbox_set_curr(vk_listbox_t *listbox, int idx);
int             vk_listbox_exec_curr(vk_listbox_t *listbox);
int             vk_listbox_set_next(vk_listbox_t *listbox);
int             vk_listbox_set_prev(vk_listbox_t *listbox);
bool            vk_listbox_item_is_separator(vk_listbox_t *listbox, int idx);
int             vk_listbox_get_metrics(vk_listbox_t *listbox,
                    int *width, int *height);
int             vk_listbox_update(vk_listbox_t *listbox);
int             vk_listbox_reset(vk_listbox_t *listbox);
int             vk_listbox_add_separator(vk_listbox_t *listbox, int style);
void            vk_listbox_destroy(vk_listbox_t *listbox);

/* vk_selectbox */
vk_selectbox_t* vk_selectbox_create(int width, int height, int mode);
int             vk_selectbox_set_style(vk_selectbox_t *selectbox, int style);
#define         vk_selectbox_set_wrap(sb, allowed) \
                    vk_listbox_set_wrap(VK_LISTBOX(sb), (allowed))
#define         vk_selectbox_set_highlight(sb, fg, bg) \
                    vk_listbox_set_highlight(VK_LISTBOX(sb), (fg), (bg))
#define         vk_selectbox_add_item(sb, item, func, anything) \
                    vk_listbox_add_item(VK_LISTBOX(sb), (item), (func), (anything))
#define         vk_selectbox_add_separator(sb, style) \
                    vk_listbox_add_separator(VK_LISTBOX(sb), (style))
#define         vk_selectbox_remove_item(sb, idx) \
                    vk_listbox_remove_item(VK_LISTBOX(sb), (idx))
#define         vk_selectbox_set_item(sb, idx, item, func, anything) \
                    vk_listbox_set_item(VK_LISTBOX(sb), (idx), (item), (func), (anything))
#define         vk_selectbox_get_item(sb, idx, buf, sz) \
                    vk_listbox_get_item(VK_LISTBOX(sb), (idx), (buf), (sz))
#define         vk_selectbox_get_item_count(sb) \
                    vk_listbox_get_item_count(VK_LISTBOX(sb))
#define         vk_selectbox_item_is_separator(sb, idx) \
                    vk_listbox_item_is_separator(VK_LISTBOX(sb), (idx))
#define         vk_selectbox_get_curr(sb) \
                    vk_listbox_get_curr(VK_LISTBOX(sb))
#define         vk_selectbox_set_curr(sb, idx) \
                    vk_listbox_set_curr(VK_LISTBOX(sb), (idx))
#define         vk_selectbox_exec_curr(sb) \
                    vk_listbox_exec_curr(VK_LISTBOX(sb))
#define         vk_selectbox_set_next(sb) \
                    vk_listbox_set_next(VK_LISTBOX(sb))
#define         vk_selectbox_set_prev(sb) \
                    vk_listbox_set_prev(VK_LISTBOX(sb))
int             vk_selectbox_toggle_item(vk_selectbox_t *selectbox, int idx);
bool            vk_selectbox_item_is_checked(vk_selectbox_t *selectbox,
                    int idx);
int             vk_selectbox_check_item(vk_selectbox_t *selectbox, int idx);
int             vk_selectbox_uncheck_item(vk_selectbox_t *selectbox, int idx);
int             vk_selectbox_uncheck_all(vk_selectbox_t *selectbox);
#define         vk_selectbox_update(sb) \
                    vk_listbox_update(VK_LISTBOX(sb))
void            vk_selectbox_destroy(vk_selectbox_t *selectbox);

/* vk_dropdown */
vk_dropdown_t*  vk_dropdown_create(int width, int max_visible);
int             vk_dropdown_set_relief_style(vk_dropdown_t *dropdown,
                    int style);
int             vk_dropdown_set_expanded(vk_dropdown_t *dropdown,
                    bool expanded);
bool            vk_dropdown_get_expanded(vk_dropdown_t *dropdown);
vk_widget_t*    vk_dropdown_get_popup(vk_dropdown_t *dropdown);
int             vk_dropdown_popup_navigate(vk_dropdown_t *dropdown,
                    int direction);
int             vk_dropdown_popup_select(vk_dropdown_t *dropdown);
#define         vk_dropdown_set_wrap(dd, allowed) \
                    vk_listbox_set_wrap(VK_LISTBOX(dd), (allowed))
#define         vk_dropdown_set_highlight(dd, fg, bg) \
                    vk_listbox_set_highlight(VK_LISTBOX(dd), (fg), (bg))
#define         vk_dropdown_add_item(dd, item, func, anything) \
                    vk_listbox_add_item(VK_LISTBOX(dd), (item), (func), (anything))
#define         vk_dropdown_remove_item(dd, idx) \
                    vk_listbox_remove_item(VK_LISTBOX(dd), (idx))
#define         vk_dropdown_get_item(dd, idx, buf, sz) \
                    vk_listbox_get_item(VK_LISTBOX(dd), (idx), (buf), (sz))
#define         vk_dropdown_get_item_count(dd) \
                    vk_listbox_get_item_count(VK_LISTBOX(dd))
#define         vk_dropdown_get_curr(dd) \
                    vk_listbox_get_curr(VK_LISTBOX(dd))
#define         vk_dropdown_set_curr(dd, idx) \
                    vk_listbox_set_curr(VK_LISTBOX(dd), (idx))
#define         vk_dropdown_set_next(dd) \
                    vk_listbox_set_next(VK_LISTBOX(dd))
#define         vk_dropdown_set_prev(dd) \
                    vk_listbox_set_prev(VK_LISTBOX(dd))
int             vk_dropdown_update(vk_dropdown_t *dropdown);
void            vk_dropdown_destroy(vk_dropdown_t *dropdown);

/* vk_frame */
vk_frame_t*     vk_frame_create(int width, int height);
int             vk_frame_set_border_style(vk_frame_t *frame, int style);
int             vk_frame_get_border_style(vk_frame_t *frame);
int             vk_frame_set_border_colors(vk_frame_t *frame,
                    short fg, short bg);
int             vk_frame_set_border_attrs(vk_frame_t *frame, attr_t attrs);
short           vk_frame_get_border_fg(vk_frame_t *frame);
short           vk_frame_get_border_bg(vk_frame_t *frame);
int             vk_frame_set_child(vk_frame_t *frame, vk_widget_t *child);
vk_widget_t*    vk_frame_get_child(vk_frame_t *frame);
int             vk_frame_update(vk_frame_t *frame);
void            vk_frame_destroy(vk_frame_t *frame);

/* vk_scroller */
vk_scroller_t*  vk_scroller_create(int flags);
int             vk_scroller_set_border_style(vk_scroller_t *scroller,
                    int style);
int             vk_scroller_set_border_colors(vk_scroller_t *scroller,
                    short fg, short bg);
int             vk_scroller_set_scroll_info(vk_scroller_t *scroller,
                    VkScrollInfoFunc func);
int             vk_scroller_set_scroll_source(vk_scroller_t *scroller,
                    vk_widget_t *source);
int             vk_scroller_update(vk_scroller_t *scroller);
void            vk_scroller_destroy(vk_scroller_t *scroller);

int             vk_widget_attach_scroller(vk_widget_t *host,
                    vk_scroller_t *scroller);
int             vk_widget_detach_scroller(vk_widget_t *host,
                    vk_scroller_t *scroller);

/* vk_window */
vk_window_t*    vk_window_create(int width, int height);
int             vk_window_set_title(vk_window_t *window, const char *title);
const char*     vk_window_get_title(vk_window_t *window);
int             vk_window_set_title_justify(vk_window_t *window, int justify);
#define         vk_window_set_border_style(w, s) \
                    vk_frame_set_border_style(VK_FRAME(w), (s))
#define         vk_window_get_border_style(w) \
                    vk_frame_get_border_style(VK_FRAME(w))
#define         vk_window_set_border_colors(w, fg, bg) \
                    vk_frame_set_border_colors(VK_FRAME(w), (fg), (bg))
#define         vk_window_set_border_attrs(w, attrs) \
                    vk_frame_set_border_attrs(VK_FRAME(w), (attrs))
#define         vk_window_get_border_fg(w) \
                    vk_frame_get_border_fg(VK_FRAME(w))
#define         vk_window_get_border_bg(w) \
                    vk_frame_get_border_bg(VK_FRAME(w))
#define         vk_window_set_child(w, child) \
                    vk_frame_set_child(VK_FRAME(w), (child))
#define         vk_window_get_child(w) \
                    vk_frame_get_child(VK_FRAME(w))
int             vk_window_set_decorate(vk_window_t *window,
                    VkWindowDecorateFunc func, void *data);
#define         vk_window_update(w) \
                    vk_frame_update(VK_FRAME(w))
void            vk_window_destroy(vk_window_t *window);

/* vk_box */
vk_box_t*       vk_box_create(int width, int height,
                    int orientation, int slots);
int             vk_box_set_homogeneous(vk_box_t *box, bool homogeneous);
int             vk_box_set_widget(vk_box_t *box, int slot,
                    vk_widget_t *widget);
vk_widget_t*    vk_box_get_widget(vk_box_t *box, int slot);
int             vk_box_get_slot_count(vk_box_t *box);
int             vk_box_set_subfocus(vk_box_t *box, int slot);
int             vk_box_get_subfocus(vk_box_t *box);
int             vk_box_update(vk_box_t *box);
void            vk_box_destroy(vk_box_t *box);

/* vk_label */
vk_label_t*     vk_label_create(int width);
int             vk_label_set_text(vk_label_t *label, const char *text);
const char*     vk_label_get_text(vk_label_t *label);
int             vk_label_set_justify(vk_label_t *label, int justify);
int             vk_label_update(vk_label_t *label);
void            vk_label_destroy(vk_label_t *label);

/* vk_textbox */
vk_textbox_t*   vk_textbox_create(int width, int height);
int             vk_textbox_set_text(vk_textbox_t *textbox, const char *text);
const char*     vk_textbox_get_text(vk_textbox_t *textbox);
int             vk_textbox_set_word_wrap(vk_textbox_t *textbox, bool enabled);
int             vk_textbox_get_line_count(vk_textbox_t *textbox);
int             vk_textbox_get_scroll_pos(vk_textbox_t *textbox);
int             vk_textbox_scroll_up(vk_textbox_t *textbox);
int             vk_textbox_scroll_down(vk_textbox_t *textbox);
int             vk_textbox_scroll_pgup(vk_textbox_t *textbox);
int             vk_textbox_scroll_pgdn(vk_textbox_t *textbox);
int             vk_textbox_scroll_home(vk_textbox_t *textbox);
int             vk_textbox_scroll_end(vk_textbox_t *textbox);
int             vk_textbox_update(vk_textbox_t *textbox);
void            vk_textbox_destroy(vk_textbox_t *textbox);

/* vk_marquee */
vk_marquee_t*   vk_marquee_create(int width);
int             vk_marquee_set_text(vk_marquee_t *marquee, const char *text);
#define         vk_marquee_get_text(m) \
                    vk_label_get_text(VK_LABEL(m))
int             vk_marquee_set_direction(vk_marquee_t *marquee, int direction);
int             vk_marquee_set_speed(vk_marquee_t *marquee, int interval);
int             vk_marquee_set_pause(vk_marquee_t *marquee, int duration);
int             vk_marquee_set_repeat(vk_marquee_t *marquee, bool repeat);
int             vk_marquee_run(vk_marquee_t *marquee);
void            vk_marquee_destroy(vk_marquee_t *marquee);

/* vk_deck */
vk_deck_t*      vk_deck_create(void);
int             vk_deck_add_widget(vk_deck_t *deck,
                    vk_widget_t *widget, int position);
int             vk_deck_remove_widget(vk_deck_t *deck,
                    vk_widget_t *widget);
int             vk_deck_set_top(vk_deck_t *deck, vk_widget_t *widget);
vk_widget_t*    vk_deck_get_top(vk_deck_t *deck);
int             vk_deck_cycle(vk_deck_t *deck, int vector);
int             vk_deck_set_shadow(vk_deck_t *deck, bool enabled);
int             vk_deck_set_shadow_colors(vk_deck_t *deck,
                    short fg, short bg);
int             vk_deck_update(vk_deck_t *deck);
vk_widget_t*    vk_deck_hit_test(vk_deck_t *deck, int x, int y);
void            vk_deck_destroy(vk_deck_t *deck);

/* vk_button */
vk_button_t*    vk_button_create(const char *text);
int             vk_button_set_text(vk_button_t *button, const char *text);
const char*     vk_button_get_text(vk_button_t *button);
int             vk_button_set_relief_style(vk_button_t *button, int style);
void            vk_button_set_pressed_colors(vk_button_t *button,
                    short fg, short bg);
int             vk_button_set_on_press(vk_button_t *button,
                    VkWidgetFunc func, void *anything);
int             vk_button_press(vk_button_t *button);
int             vk_button_release(vk_button_t *button);
int             vk_button_update(vk_button_t *button);
void            vk_button_destroy(vk_button_t *button);

/* vk_filler */
vk_filler_t*    vk_filler_create(void);
void            vk_filler_destroy(vk_filler_t *filler);

/* vk_input */
vk_input_t*     vk_input_create(int width);
int             vk_input_set_text(vk_input_t *input, const char *text);
const char*     vk_input_get_text(vk_input_t *input);
int             vk_input_set_relief_style(vk_input_t *input, int style);
int             vk_input_set_max_length(vk_input_t *input, int max);
int             vk_input_insert_char(vk_input_t *input, int ch);
int             vk_input_backspace(vk_input_t *input);
int             vk_input_delete(vk_input_t *input);
int             vk_input_move_cursor(vk_input_t *input, int offset);
int             vk_input_home(vk_input_t *input);
int             vk_input_end(vk_input_t *input);
int             vk_input_clear(vk_input_t *input);
void            vk_input_show_cursor(vk_input_t *input, bool visible);
int             vk_input_update(vk_input_t *input);
void            vk_input_destroy(vk_input_t *input);

/* vk_activity */
vk_activity_t*  vk_activity_create(void);
int             vk_activity_set_style(vk_activity_t *activity, int style);
int             vk_activity_get_style(vk_activity_t *activity);
int             vk_activity_set_speed(vk_activity_t *activity, int interval);
int             vk_activity_start(vk_activity_t *activity);
int             vk_activity_stop(vk_activity_t *activity);
bool            vk_activity_is_running(vk_activity_t *activity);
int             vk_activity_run(vk_activity_t *activity);
void            vk_activity_destroy(vk_activity_t *activity);

/* vk_menubar */
vk_menubar_t*   vk_menubar_create(int width);
int             vk_menubar_add_item(vk_menubar_t *menubar,
                    char *name, VkWidgetFunc func, void *anything);
int             vk_menubar_get_item_count(vk_menubar_t *menubar);
int             vk_menubar_get_curr(vk_menubar_t *menubar);
int             vk_menubar_set_curr(vk_menubar_t *menubar, int idx);
int             vk_menubar_set_next(vk_menubar_t *menubar);
int             vk_menubar_set_prev(vk_menubar_t *menubar);
int             vk_menubar_exec_curr(vk_menubar_t *menubar);
int             vk_menubar_set_highlight(vk_menubar_t *menubar,
                    int fg, int bg);
int             vk_menubar_set_focused(vk_menubar_t *menubar, bool focused);
bool            vk_menubar_get_focused(vk_menubar_t *menubar);
int             vk_menubar_hit_test(vk_menubar_t *menubar, int x);
int             vk_menubar_get_item_position(vk_menubar_t *menubar,
                    int idx, int *x);
int             vk_menubar_update(vk_menubar_t *menubar);
int             vk_menubar_reset(vk_menubar_t *menubar);
void            vk_menubar_destroy(vk_menubar_t *menubar);

/* vk_filedialog */
vk_filedialog_t* vk_filedialog_create(int width, int height,
                    int style, bool multiselect);
int             vk_filedialog_set_path(vk_filedialog_t *dialog,
                    const char *path);
const char*     vk_filedialog_get_path(vk_filedialog_t *dialog);
const char*     vk_filedialog_get_selected(vk_filedialog_t *dialog);
int             vk_filedialog_set_wrap(vk_filedialog_t *dialog,
                    bool allowed);
int             vk_filedialog_set_colors(vk_filedialog_t *dialog,
                    short fg, short bg);
int             vk_filedialog_set_highlight(vk_filedialog_t *dialog,
                    short fg, short bg);
int             vk_filedialog_set_button_colors(vk_filedialog_t *dialog,
                    short fg, short bg);
int             vk_filedialog_set_button_attrs(vk_filedialog_t *dialog,
                    attr_t attrs);
vk_listbox_t*   vk_filedialog_get_file_list(vk_filedialog_t *dialog);
int             vk_filedialog_update(vk_filedialog_t *dialog);
void            vk_filedialog_destroy(vk_filedialog_t *dialog);

/* vk_calendar */
vk_calendar_t*  vk_calendar_create(void);
int             vk_calendar_set_month(vk_calendar_t *calendar,
                    int month, int year);
int             vk_calendar_get_month(vk_calendar_t *calendar,
                    int *month, int *year);
int             vk_calendar_prev_month(vk_calendar_t *calendar);
int             vk_calendar_next_month(vk_calendar_t *calendar);
int             vk_calendar_set_highlight(vk_calendar_t *calendar,
                    short fg, short bg);
int             vk_calendar_set_highlight_attrs(vk_calendar_t *calendar,
                    attr_t attrs);
int             vk_calendar_set_dimmed(vk_calendar_t *calendar,
                    short fg, short bg);
int             vk_calendar_set_dimmed_attrs(vk_calendar_t *calendar,
                    attr_t attrs);
int             vk_calendar_set_header_colors(vk_calendar_t *calendar,
                    short fg, short bg);
int             vk_calendar_set_header_attrs(vk_calendar_t *calendar,
                    attr_t attrs);
int             vk_calendar_update(vk_calendar_t *calendar);
void            vk_calendar_destroy(vk_calendar_t *calendar);

/* vk_popup */
vk_popup_t*     vk_popup_create(int width, int height, int style, ...);
int             vk_popup_set_client(vk_popup_t *popup,
                    vk_widget_t *widget);
vk_widget_t*    vk_popup_get_client(vk_popup_t *popup);
int             vk_popup_add_button(vk_popup_t *popup, const char *text);
vk_button_t*    vk_popup_get_button(vk_popup_t *popup, int index);
int             vk_popup_get_button_count(vk_popup_t *popup);
vk_box_t*       vk_popup_get_button_bar(vk_popup_t *popup);
int             vk_popup_get_result(vk_popup_t *popup);
int             vk_popup_close(vk_popup_t *popup, int result);
int             vk_popup_set_colors(vk_popup_t *popup,
                    short fg, short bg);
int             vk_popup_set_button_colors(vk_popup_t *popup,
                    short fg, short bg);
int             vk_popup_set_button_attrs(vk_popup_t *popup,
                    attr_t attrs);
#define         vk_popup_set_title(p, t) \
                    vk_window_set_title(VK_WINDOW(p), (t))
#define         vk_popup_get_title(p) \
                    vk_window_get_title(VK_WINDOW(p))
#define         vk_popup_set_border_style(p, s) \
                    vk_window_set_border_style(VK_WINDOW(p), (s))
#define         vk_popup_set_border_colors(p, fg, bg) \
                    vk_window_set_border_colors(VK_WINDOW(p), (fg), (bg))
#define         vk_popup_set_border_attrs(p, a) \
                    vk_window_set_border_attrs(VK_WINDOW(p), (a))
int             vk_popup_update(vk_popup_t *popup);
void            vk_popup_destroy(vk_popup_t *popup);

#endif
