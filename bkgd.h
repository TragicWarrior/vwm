#ifndef _H_VWM_BKGD_
#define _H_VWM_BKGD_

#include <curses.h>

void    vwm_bkgd_simple_normal(int screen_id);
void    vwm_bkgd_simple_winman(int screen_id);

int     vwm_bkgd_bricks(WINDOW *bkgd_window, void *arg);

#endif
