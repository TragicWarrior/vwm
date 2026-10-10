#ifndef _VWM_MANAGE_UI_COMMON_H_
#define _VWM_MANAGE_UI_COMMON_H_

#include <vdk.h>

/*
    UI primitives shared by the Manage Settings / Hotkeys / Apps dialogs,
    extracted from their former per-file copies (TODO S2).
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
   dialog ("You have unsaved changes." / "Discard changes and close?")
   with Discard / Cancel buttons (button 0 active), returning it.  The
   caller sets its kmio handler, stores the pointer, and resets its own
   active-button index.  Shared by Settings / Hotkeys / Apps. */
vk_popup_t *vwm_confirm_popup_show(void);

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

#endif
