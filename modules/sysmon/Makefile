CFLAGS = -Wall -O2
DEFS = -D_REENTRANT -D_GNU_SOURCE
prefix = /usr/local
libdir = ${prefix}/lib
includedir = ${prefix}/include -I../..
bindir = ${prefix}/bin
MODDIR = /usr/lib/vwm/modules

makefile: all

all: clean sysmon
	strip sysmon.so

wide: clean sysmon_wide
	strip sysmon_wide.so

debug: clean sysmon

debug_wide: clean sysmon_wide

sysmon:
	gcc $(CFLAGS) $(DEFS) -c -fpic *.c `pkg-config --cflags glib-2.0` `pkg-config --cflags gmodule-2.0` `pkg-config --cflags libgtop-2.0`
	gcc $(CFLAGS) -shared -o sysmon.so *.o

sysmon_wide:
	gcc $(CFLAGS) $(DEFS) -D_VIPER_WIDE -c -fpic *.c `pkg-config --cflags glib-2.0` `pkg-config --cflags gmodule-2.0` `pkg-config --cflags libgtop-2.0`
	gcc $(CFLAGS) -shared -o sysmon_wide.so *.o

clean:
	rm -f *.o
	rm -f *.so

install:
	chmod 755 sysmon.so
	cp -f sysmon.so $(MODDIR)

install_wide:
	chmod 755 sysmon_wide.so
	cp -f sysmon_wide.so $(MODDIR)/sysmon.so
