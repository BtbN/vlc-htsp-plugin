PREFIX = /usr/local
CC = gcc
AR = ar
RANLIB = ranlib
INSTALL = install
CFLAGS = -std=c99 -D_GNU_SOURCE -pipe -O2 -Wall -Wextra -DPIC -fPIC -I.

SRC = htsmsg.o \
      htsmsg_binary.o \
      htsbuf.o \
      htsstr.o \
      net_posix.o \
      sha1.o

all: libhts.a

clean:
	rm -f -- *.o *.a

mostlyclean: clean

libhts.a: $(SRC:%.c=%.o)
	$(AR) rcs $@ $^
	$(RANLIB) $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

