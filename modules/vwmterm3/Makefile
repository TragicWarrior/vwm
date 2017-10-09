CFLAGS = -Wall -std=gnu99 -O2
DEFS = -D_REENTRANT -D_GNU_SOURCE
prefix = /usr/local
libdir = ${prefix}/lib
includedir = -I${prefix}/include -I../..
bindir = ${prefix}/bin
MODDIR = /usr/lib/vwm/modules

makefile: all

all: clean vwmterm3
	strip vwmterm3.so

wide: clean vwmterm3_wide
	strip vwmterm3_wide.so

debug: clean vwmterm3

vwmterm3:
	gcc $(CFLAGS) $(DEFS) $(includedir) -c -fpic *.c 
	gcc $(CFLAGS) -shared -o vwmterm3.so *.o

vwmterm3_wide:
	gcc $(CFLAGS) $(DEFS) $(includedir) -D_VIPER_WIDE -c -fpic *.c 
	gcc $(CFLAGS) -shared -o vwmterm3_wide.so *.o

clean:
	rm -f *.o
	rm -f *.so

install:
	chmod 755 vwmterm3.so
	strip vwmterm3.so
	cp -f vwmterm3.so $(MODDIR)

install_wide:
	chmod 755 vwmterm3_wide.so
	cp -f vwmterm3_wide.so $(MODDIR)/vwmterm.so
