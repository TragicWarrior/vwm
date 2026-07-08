#ifndef _VWM_SCREENSAVER_H_
#define _VWM_SCREENSAVER_H_

#include <inttypes.h>
#include <stdbool.h>

#include <ncursesw/curses.h>

/* record user activity (resets the idle timer) */
void    vwm_screensaver_note_activity(void);

/* called from the clock tick; launches the saver once idle exceeds the
   configured timeout */
void    vwm_screensaver_tick(void);

/* start the saver immediately if a command is configured (manual lock) */
void    vwm_screensaver_activate(void);

/* true while the screensaver is running (input is locked to it) */
bool    vwm_screensaver_is_active(void);

/* forward input to the running saver: keys go to its terminal; mouse is
   swallowed.  KEY_RESIZE is handled by vwm_screensaver_resize() instead. */
void    vwm_screensaver_input(int32_t keystroke, MEVENT *mouse_event);

/* resize the fullscreen saver overlay -- and the locked program's vterm -- to
   the current screen size (e.g. after a dtach reattach onto a different-size
   terminal).  Touches only the overlay, never the hidden desktop beneath it. */
void    vwm_screensaver_resize(void);

#endif
