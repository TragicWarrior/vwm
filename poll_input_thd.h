#ifndef _POLL_INPUT_THD_H_
#define _POLL_INPUT_THD_H_

#include <vdk.h>

#include "protothread.h"

pt_t    vwm_poll_input(void * const env);

void    vwm_cancel_drag_for_widget(vk_widget_t *widget);

#endif
