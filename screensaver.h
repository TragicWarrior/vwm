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

/* forward input to the running saver: keys go to its terminal, mouse and
   resize are swallowed */
void    vwm_screensaver_input(int32_t keystroke, MEVENT *mouse_event);

#endif
