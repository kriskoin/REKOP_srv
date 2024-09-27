#
# GNU Make Makefile for PPLIB under Linux.
#

SHELL = /bin/sh

.SUFFIXES:
.SUFFIXES: .cpp .c .o

OUTPUTLIBS = libpp.a

OBJECTS = array.o common.o crc.o fnames.o hand.o gunzip.o iptools.o kprintf.o \
		  llip.o mem.o parms.o pktpool.o poker.o pplib.o proc.o qsort.o roboplay.o \
          seconds.o serverip.o stackcr.o tree.o
# clientip.o

HEADERS = gamedata.h hand.h llip.h poker.h pplib.h roboplay.h zconf.h zlib.h

#CC	 = gcc
CC	= g++
# Optimization options: -O0 disables optimizations, -O3 is max optimizations.
# -g enables debug information
#CFLAGS = -DLINUX=1 -O0 -g
#CFLAGS = -DLINUX=1 -D_GNU_SOURCE -D_REENTRANT -O3 -g
CFLAGS = -DLINUX=1 -D_GNU_SOURCE -D_REENTRANT -DINCL_STACK_CRAWL -g
#24/01/01 kriskoin:
#CPPFLAGS = -mcpu=pentiumpro
#CPPFLAGS = -m486
CPPFLAGS = -mcpu=i486
CPLUS_INCLUDE_PATH = .:/usr/local/ssl/include/
export CPLUS_INCLUDE_PATH

.cpp.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

.c.o:
	$(CC) $(CFLAGS) -c $<

all: Makefile $(OUTPUTLIBS)
.PHONY: all

install: all
.PHONY: install

clean:
	rm -f $(OUTPUTLIBS) *.o core *.a
.PHONY: clean

##########################
# Linking rules.
libpp.a: Makefile $(OBJECTS)
	ar rc $@ $(OBJECTS)
	ranlib $@
	cp $@ /usr/local/lib/$@
	 >/dev/null 2>&1

##########################
# Compilation rules.

$(OBJECTS): $(HEADERS)
