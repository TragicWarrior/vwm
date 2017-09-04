#ifndef _CLOCK_THD_H_
#define _CLOCK_THD_H_

#include <glib.h>
#include <protothread.h>

struct _clock_data_s
{
    GTimer      *timer;
    gfloat      elapsed;
};

typedef struct _clock_data_s    clock_data_t;

pt_t vwm_clock_driver(void * const env);

#endif
