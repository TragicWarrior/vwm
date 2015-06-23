#ifndef _VWM_PSTHREAD_H_
#define _VWM_PSTHREAD_H_

#include <stdint.h>

#include <glib.h>

#include <pseudo.h>

/* pseudo threads */
int16_t	vwm_throttle_ui(ps_task_t *task,void *anything);
int16_t	vwm_poll_input(ps_task_t *task,void *anything);
int16_t  vwm_clock_driver(ps_task_t *task,void *anything);

#endif
