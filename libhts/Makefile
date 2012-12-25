PREFIX = /usr/local
CC = gcc
AR = ar
RANLIB = ranlib
INSTALL = install
CFLAGS = -std=c99 -D_GNU_SOURCE -pipe -O2 -Wall -Wextra -DPIC -fPIC -I.

SRC = htsmsg.c \
      htsmsg_binary.c \
      htsbuf.c \
      htsstr.c \
      net_posix.c \
      sha1.c

all: libhts.a

clean:
	rm -f -- *.o *.a

mostlyclean: clean

libhts.a: $(SRC:%.c=%.o)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

