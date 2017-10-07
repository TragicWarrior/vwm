CFLAGS = -std=gnu99 -Wall -O2 -ggdb
DEFS = -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -D_GNU_SOURCE -D_VWM_SCREENSAVER_TIMEOUT=5 -D_DEBUG
LIBS = -lncurses -lviper -ldl -lmenu -lform -lgpm -lprotothread -lrt
WLIBS = -lncursesw -lviper -ldl -lmenu -lform -lgpm
PREFIX = /usr/local/
libdir = ${prefix}/lib
INCLUDES = -I./
MODDIR = /usr/lib/vwm/modules

makefile: all

all: clean vwm keycode_tool vwmterm

wide: clean vwm_wide keycode_tool_wide vwmterm_wide

.PHONY: vwmterm vwmterm_wide keycode_tool keycode_tool_wide

vwm_wide:
	sh -c "echo '#ifndef _VIPER_WIDE'; echo '#define _VIPER_WIDE 1';echo '#endif';echo '#include <vwm.h>'" > vwm_wide.h
	gcc $(CFLAGS) $(DEFS) $(INCLUDES) -D_VIPER_WIDE *.c -o vwm_wide $(WLIBS)

vwm:
	gcc -rdynamic $(CFLAGS) $(DEFS) $(INCLUDES) *.c -o vwm $(LIBS)

vwmterm:
	cd modules/vwmterm3 && $(MAKE)

vwmterm_wide:
	cd modules/vwmterm3 && $(MAKE) wide

keycode_tool:
	cd keycodes && $(MAKE)

keycode_tool_wide:
	cd keycodes && $(MAKE) wide

clean: 
	rm -f *.o
	rm -f vwm
	rm -f vwm_wide
	rm -f vwm_wide.h

install:
	mkdir -p $(MODDIR)
	chmod 644 vwm.h
	cp -f vwm.h ${PREFIX}/include
	chmod 755 vwm
	cp -f vwm ${PREFIX}/bin
	cd modules/vwmterm3 && $(MAKE) install

install_wide:
	mkdir -p $(MODDIR)
	chmod 755 vwm_wide
	cp -f vwm_wide ${PREFIX}/bin
	cd modules/vwmterm3 && $(MAKE) install_wide
