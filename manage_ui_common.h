#ifndef _VWM_MANAGE_UI_COMMON_H_
#define _VWM_MANAGE_UI_COMMON_H_

#include <vdk.h>

/*
    UI primitives shared by the Manage Settings / Hotkeys / Apps / Windows
    dialogs, extracted from their former per-file copies (TODO S2).
    Popup close and key handling live here too.  Each dialog keeps a
    thin wrapper where the kmio signature is fixed to (object, key).
*/

/* Take a popup off the screen and destroy it, and clear the caller's
   pointer to it.  Does nothing when *popup is already NULL.  The caller
   repaints whatever was underneath. */
void    vwm_popup_dismiss(vk_popup_t **popup);

/* Paint a popup's buttons so that button `active` stands out (yellow
   on white) and the others are plain (black on white).  Updates the
   buttons only; the caller updates the popup and refreshes the screen
   when it is ready to show the result. */
void    vwm_popup_buttons_paint(vk_popup_t *popup, int active);

/* Which of a dialog's `count` bottom-row buttons is under column `col`?
   `col` is counted across the dialog's interior, which is where the
   button row starts.  Returns the button's index in `buttons`, or -1
   for the gap between the left and right groups.

   The answer comes from where each button actually is, so it cannot
   drift from the layout the way a table of column numbers can. */
int     vwm_button_bar_hit(vk_button_t **buttons, int count, int col);

/* The first widget in `layers` (NULL entries are skipped) that the
   screen cell (mx, my) falls inside, or NULL.  A dialog lists its
   popups topmost first and itself last, and so answers "what of mine
   is under the pointer?" in one call. */
vk_widget_t *vwm_layers_hit(vk_widget_t **layers, int count,
                int mx, int my);

/* vk_scroller content/scroll-extent callback for a listbox child: reports
   the item count as content height, the listbox width as content width,
   and the current item as the vertical scroll position. */
void    vwm_listbox_scroll_info(vk_widget_t *child,
            int *content_h, int *content_w, int *scroll_y, int *scroll_x);

/* Build, center, and attach the shared "terminal becomes too small"
   warning popup, returning it.  The caller sets its kmio handler and
   stores the pointer.  Shared by Manage Settings / Hotkeys / Apps. */
vk_popup_t *vwm_warning_popup_show(void);

/* Build, center, and attach a shared error popup showing `msg` at the
   given size (message length varies per dialog).  The caller sets its
   kmio handler and stores the pointer.  Shared by Settings / Hotkeys. */
vk_popup_t *vwm_error_popup_show(const char *msg, int popup_w, int popup_h);

/* Build, center, and attach the shared white-on-blue " Saved "
   confirmation popup showing `msg`.  The caller sets its kmio handler
   and stores the pointer.  Shared by Settings / Hotkeys / Apps. */
vk_popup_t *vwm_saved_popup_show(const char *msg);

/* Build, center, and attach the shared red-on-white " Confirm " discard
   dialog ("You have unsaved changes." / `question`, such as "Discard
   changes and close?") with Discard / Cancel buttons (button 0 active),
   returning it.  The
   caller sets its kmio handler, stores the pointer, and resets its own
   active-button index.  Shared by Settings / Hotkeys / Apps. */
vk_popup_t *vwm_confirm_popup_show(const char *question);

/* Focus targets for the Load popup's Tab cycle (file browser -> OK ->
   Cancel); mirrors the per-dialog focus orderings in Hotkeys / Apps. */
enum {
    VWM_LOAD_FOCUS_FILEDIALOG = 0,
    VWM_LOAD_FOCUS_OK,
    VWM_LOAD_FOCUS_CANCEL,
    VWM_LOAD_FOCUS_COUNT
};

/* Build, center, and attach the shared white-on-blue file-load popup
   titled `title`, holding a vk_filedialog rooted at the directory of
   `cur_path`; the filedialog is returned through *filedialog.  The
   caller sets the popup's kmio, stores the pointer, and refreshes.
   Shared by Settings / Hotkeys / Apps. */
vk_popup_t *vwm_load_popup_show(const char *title,
    vk_filedialog_t **filedialog, const char *cur_path);

/* Repaint the load filedialog's OK / Cancel buttons and file-list
   highlight for the given focus (a VWM_LOAD_FOCUS_* value).  Used by the
   Hotkeys / Apps load dialogs, which Tab between browser and buttons. */
void vwm_load_popup_paint_focus(vk_filedialog_t *filedialog, int focus);

/* Close `popup` and then run `repaint` (the dialog underneath).
   `repaint` may be NULL.  Does nothing when the popup is not open. */
void    vwm_popup_close(vk_popup_t **popup, void (*repaint)(void));

/* Extra keys, and two windows-dialog exceptions, for the notice and
   two-button handlers.  Settings, Hotkeys and Apps pass 0. */
#define VWM_POPUP_KEY_ENTER     (1 << 0)    /* 1: keypad Enter counts */
#define VWM_POPUP_KEY_BTAB      (1 << 1)    /* 2: Shift-Tab moves buttons */
#define VWM_POPUP_QUIET_FIRST   (1 << 2)    /* 4: first button does not repaint */
#define VWM_POPUP_QUIET_MOVE    (1 << 3)    /* 8: button move does not redraw */

/* Keys for a one-button notice (error, warning, saved).  Esc, Enter or
   Space closes `popup` and runs `repaint`.  Every other key is swallowed.
   VWM_POPUP_KEY_ENTER also accepts the keypad Enter. */
int     vwm_notice_popup_kmio(vk_popup_t **popup, int32_t keystroke,
            void (*repaint)(void), int flags);

/* Keys for a popup with two buttons.  Esc closes and repaints.  Tab,
   Left and Right move to the other button (Shift-Tab too, when
   VWM_POPUP_KEY_BTAB is set).  Enter or Space presses the focused
   button (keypad Enter too, when VWM_POPUP_KEY_ENTER is set).
   `on_first` runs when the first button is pressed, after the popup
   has been destroyed.  VWM_POPUP_QUIET_FIRST skips the repaint on that
   button.  VWM_POPUP_QUIET_MOVE refreshes the screen without redrawing
   the popup. */
int     vwm_two_button_kmio(vk_popup_t **popup, int *active,
            int32_t keystroke, void (*on_first)(void),
            void (*repaint)(void), int flags);

/* How a load popup treats Tab, an empty selection, and a directory.
   Settings passes 0: every key goes to the file dialog, an empty
   selection leaves the popup open, and OK does not itself refuse a
   directory.  Hotkeys and Apps set all three. */
#define VWM_LOAD_TAB_CYCLES     (1 << 0)    /* 1: Tab walks file/OK/Cancel */
#define VWM_LOAD_CLOSE_EMPTY    (1 << 1)    /* 2: NULL path or selection closes */
#define VWM_LOAD_REJECT_DIR     (1 << 2)    /* 4: OK ignores a directory */

/* Take the load popup down, drop the caller's filedialog pointer, and
   repaint the dialog.  Does nothing when the popup is not open. */
void    vwm_load_popup_close(vk_popup_t **popup,
            vk_filedialog_t **filedialog, void (*repaint)(void));

/* Build the selected file's path, destroy the popup, then call
   `on_file` with that path.  See VWM_LOAD_CLOSE_EMPTY and
   VWM_LOAD_REJECT_DIR for the two cases that do not load a file. */
void    vwm_load_popup_ok(vk_popup_t **popup, vk_filedialog_t **filedialog,
            void (*on_file)(const char *path), void (*repaint)(void),
            int flags);

/* Keys for the load popup.  Esc closes it.  With VWM_LOAD_TAB_CYCLES,
   Tab moves `focus` (a VWM_LOAD_FOCUS_* value) between the file list,
   OK and Cancel, and Enter or Space on a button runs that button.
   Otherwise every key is given to the file dialog.  Enter on a plain
   file loads it.  `repaint_popup` redraws the popup when it stays open. */
int     vwm_load_popup_kmio(vk_popup_t **popup,
            vk_filedialog_t **filedialog, int *focus, int32_t keystroke,
            void (*on_file)(const char *path), void (*repaint_dialog)(void),
            void (*repaint_popup)(void), int flags);

#endif
