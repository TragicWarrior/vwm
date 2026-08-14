#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <inttypes.h>

#include <ncursesw/curses.h>

#include <vdk.h>
#include <vterm.h>

void    vwmterm_init_keycodes(void);

/* subscribe the vterm to OSC 52 clipboard events (libvterm 10.9+) */
void    vwmterm_bind_vterm(vterm_t *vterm);

void    vwmterm_window_update(vwmterm_data_t *vwmterm_data);

int     vwmterm_ON_KEYSTROKE(vk_object_t *object, int32_t keystroke);

int     vwmterm_ON_RESIZE(vk_object_t *object, int event, void *anything);
int     vwmterm_ON_RECREATE(vk_object_t *object, int event, void *anything);
int     vwmterm_ON_SCREEN_RESIZED(vk_object_t *object, int event, void *anything);
int	    vwmterm_ON_CLOSE(vk_object_t *object, int event, void *anything);

#endif
