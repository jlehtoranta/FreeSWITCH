MODNAME=mod_ofono
SVNDEF:=-D'OFONO_SVN_VERSION="$(shell svnversion -n .)"'
PKG_INC_CFLAGS=$(shell pkg-config --cflags glib-2.0 dbus-1)
PKG_INC_LDFLAGS=$(shell pkg-config --libs glib-2.0 gio-2.0 dbus-glib-1 dbus-1)

LOCAL_SOURCES=mod_ofono.c ofono_protocol.c gdbus/mainloop.c gdbus/object.c gdbus/polkit.c gdbus/watch.c

LOCAL_CFLAGS=$(SVNDEF) $(PKG_INC_CFLAGS) -I../../../../libs/spandsp/src -I../../../..//libs/tiff-3.8.2/libtiff
LOCAL_LDFLAGS=-L../../../../libs/spandsp/src -lasound -lspandsp $(PKG_INC_LDFLAGS)

LOCAL_OBJS=ofono_protocol.o gdbus/mainloop.o gdbus/object.o gdbus/polkit.o gdbus/watch.o

include ../../../../build/modmake.rules
