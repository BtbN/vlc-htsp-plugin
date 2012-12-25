PREFIX = /usr
LD = ld
CC = gcc
INSTALL = install
CFLAGS = -g -pipe -O2 -Wall -Wextra -pedantic
LDFLAGS =
VLC_PLUGIN_CFLAGS := $(shell pkg-config --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell pkg-config --libs vlc-plugin)

libdir = $(PREFIX)/lib
plugindir = $(libdir)/vlc/plugins

override CFLAGS += -std=gnu99 -DPIC -fPIC -I.
override LDFLAGS += -Wl,-no-undefined,-z,defs

override CFLAGS += -DMODULE_STRING=\"htsp-plugin\" $(VLC_PLUGIN_CFLAGS)
override LDFLAGS += $(VLC_PLUGIN_LIBS) libhts/libhts.a

TARGETS = libhtsp_plugin.so
SOURCES = vlc-htsp-plugin.c

all: libhtsp_plugin.so

install: all
	mkdir -p -- $(DESTDIR)$(plugindir)/access
	$(INSTALL) --mode 0755 libhtsp_plugin.so $(DESTDIR)$(plugindir)/access

install-strip:
	$(MAKE) install INSTALL="$(INSTALL) -s"

uninstall:
	rm -f $(plugindir)/codec/libhtsp_plugin.so

clean:
	rm -f -- libhtsp_plugin.so *.o
	make -C libhts clean

mostlyclean: clean

%.o: %.c
	$(CC) $(CFLAGS) -c $<

libhtsp_plugin.so: libhts $(SOURCES:%.c=%.o)
	$(CC) -shared -o $@ $(SOURCES:%.c=%.o) $(LDFLAGS)

libhts:
	make -C libhts all

.PHONY: all install install-strip uninstall clean mostlyclean libhts

