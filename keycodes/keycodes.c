#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>

#include <glib.h>
#ifdef _VIPER_WIDE
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

gint32 keystroke_pop(void);

int main(void)
{
   gint32         keystroke;
   WINDOW         *window;
   struct pollfd  fds;

   window=initscr();
 	keypad(window,TRUE);
	nodelay(window,TRUE);
   noecho();
   raw();

   memset(&fds,0,sizeof(fds));
   fds.fd=STDIN_FILENO;
   fds.events=POLLIN;

   printw("press ESC to exit\n\r");
   refresh();

   do
	{
      while(poll(&fds,1,25000)==0);
		keystroke=keystroke_pop();
		printw("%x\n\r",keystroke);
      refresh();
	}
   while(keystroke!=0x1b);	

   endwin();

	return 0;
}

gint32 keystroke_pop(void)
{
	gint32	keystroke=0;
	gint32	key_code=0;

	key_code=getch();
	if(key_code==-1) return -1;
	
	if(key_code!=27) return key_code;
	
	keystroke=27;
	key_code=getch();
	if(key_code==-1) return keystroke;
	keystroke=keystroke | (key_code<<8);

	key_code=getch();
	if(key_code==-1) return keystroke;
	keystroke=keystroke | (key_code<<16);

	key_code=getch();
	if(key_code==-1) return keystroke;
	keystroke=keystroke | (key_code<<24);

	return keystroke;
}

	
