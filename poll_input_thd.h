#ifndef _POLL_INPUT_THD_H_
#define _POLL_INPUT_THD_H_

#include <vdk.h>

#include "protothread.h"

pt_t    vwm_poll_input(void * const env);

/* give the input task a turn now.  call it after putting a key where no
   descriptor will announce it -- ungetch() -- so it is handled at once
   and not at the next heartbeat. */
void    vwm_input_wake(void);

void    vwm_cancel_drag_for_widget(vk_widget_t *widget);

void    vwm_set_scroll_drag_cb(void (*cb)(vk_widget_t *widget, int mx, int my));
void    vwm_begin_scroll_drag(vk_widget_t *widget);

#endif
