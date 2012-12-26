PREFIX = /usr
LD = ld
CC = gcc
CXX = g++
INSTALL = install
CFLAGS = -g -pipe -O2 -Wall -Wextra -std=gnu99 -DPIC -fPIC -I.
CXXFLAGS = -g -pipe -O2 -Wall -Wextra -std=gnu++0x -DPIC -fPIC -I.
LDFLAGS = -Wl,-no-undefined,-z,defs
VLC_PLUGIN_CFLAGS := $(shell pkg-config --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell pkg-config --libs vlc-plugin)

libdir = $(PREFIX)/lib
plugindir = $(libdir)/vlc/plugins

override CFLAGS += -DMODULE_STRING=\"htsp\" $(VLC_PLUGIN_CFLAGS)
override CXXFLAGS += -DMODULE_STRING=\"htsp\" $(VLC_PLUGIN_CFLAGS)
override LDFLAGS += $(VLC_PLUGIN_LIBS) libhts/libhts.a

TARGETS = libhtsp_plugin.so
C_SOURCES = 
CXX_SOURCES = vlc-htsp-plugin.cpp

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

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

libhtsp_plugin.so: libhts $(C_SOURCES:%.c=%.o) $(CXX_SOURCES:%.cpp=%.o)
	$(CXX) -shared -o $@ $(C_SOURCES:%.c=%.o) $(CXX_SOURCES:%.cpp=%.o) $(LDFLAGS)

libhts:
	make -C libhts all

.PHONY: all install install-strip uninstall clean mostlyclean libhts

