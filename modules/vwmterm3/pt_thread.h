#ifndef _VWMTERM_THD_H_
#define _VWMTERM_THD_H_


#include "protothread.h"

pt_t vwmterm_thd(void * const env);

/* give this terminal's task a turn now.  call it when the terminal is
   unfrozen (SELECT mode ends): the task was waiting, and output may
   have piled up on the pty meanwhile. */
void vwmterm_wake(void *vwmterm_data);

#endif
