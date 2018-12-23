#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <stdint.h>

#include <ncursesw/curses.h>

uint32_t keystroke_pop(int *retval);

int main(void)
{
    uint32_t        keystroke;
    WINDOW          *window;
    struct pollfd   fds;
    int             retval = 0;

    window = initscr();
    keypad(window, TRUE);
	nodelay(window, TRUE);
    noecho();
    raw();

    memset(&fds, 0, sizeof(fds));
    fds.fd = STDIN_FILENO;
    fds.events = POLLIN;

    printw("press ESC to exit\n\r");
    refresh();

    do
	{
        while(poll(&fds, 1, 25000) == 0);
		keystroke = keystroke_pop(&retval);
		printw("%x\n\r", keystroke);
        refresh();
	}
    while(keystroke != 0x1b);

   endwin();

	return 0;
}

uint32_t
keystroke_pop(int *retval)
{
	uint32_t    keystroke = 0;
	uint32_t	key_code = 0;
    int         i;


	key_code = getch();
	if(key_code == -1)
    {
        *retval = 1;
        return;
    }

	if(key_code != 27) return key_code;

	keystroke = 27;

    for(i = 0; i < 3; i++)
    {
        key_code = getch();
        if(key_code == -1) return keystroke;

        keystroke = keystroke << 8;
        keystroke |= (uint8_t)key_code;
    }

/*
    for(i = 0; i < 3; i++)
    {
	    key_code = getch();
        if(key_code == -1) return keystroke;

        keystroke |= key_code << (8 * (i + 1));
    }
*/
	return keystroke;
}

	
