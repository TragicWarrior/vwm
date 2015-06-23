CFLAGS = -Wall
INCL_PKG_CFG = `pkg-config --cflags-only-I glib-2.0 ncurses`
LIBS_PKG_CFG = `pkg-config --libs-only-l glib-2.0 ncurses`
DEFS = -D_GNU_SOURCE 
LIBS = $(LIBS_PKG_CFG) -lncurses 
WLIBS = $(LIBS_PKG_CFG) -lncursesw

makefile: all

all: clean keycodes
	strip keycodes

wide: clean keycodes_wide
	strip keycodes_wide

keycodes:
	gcc $(CFLAGS) $(DEFS) $(INCL_PKG_CFG) *.c -o keycodes $(LIBS)

keycodes_wide:
	gcc $(CFLAGS) $(DEFS) -D_VIPER_WIDE $(INCL_PKG_CFG) *.c -o keycodes_wide $(WLIBS)

clean:
	rm -f *.o
	rm -f keycodes
	rm -f keycodes_wide
