#
# GNU Make Makefile for PokerSrv under Linux.
#

SHELL = /bin/sh

.SUFFIXES:
.SUFFIXES: .cpp .c .o

PROGS = pokersrv

OBJECTS = cardroom.o commonro.o deck.o ecash.o\
		ecash2.o game.o holdem.o\
		logging.o omaha.o player.o plradmin.o\
		pokersrv.o pot.o rng.o\
		sdb.o stud7.o table.o waitlist.o OPA.o

LIB = pokersrv.a

PPLIB = ../pplib

# LeakTracer.o

HEADERS = incl/cardroom.h incl/commonro.h incl/deck.h incl/ecash.h\
		incl/game.h incl/holdem.h incl/logging.h incl/omaha.h\
		incl/player.h incl/pokersrv.h incl/pot.h incl/sdb.h\
		incl/stud7.h incl/table.h incl/pot.h incl/OPA.h \
		$(PPLIB)/pplib.h $(PPLIB)/llip.h $(PPLIB)/gamedata.h

CC	 = g++

# Optimization options: -O0 disables optimizations, -O3 is max optimizations.
# -g enables debug information
#CFLAGS = -DLINUX=1 -O0 -g
CFLAGS = -DLINUX=1 -D_GNU_SOURCE -D_REENTRANT -DINCL_STACK_CRAWL -O3 -g

#24/01/01 kriskoin:
# problems we've never seen before: hung thread on the weekend
# and negative in-play balance for boatman today.
#CPPFLAGS = -mcpu=pentiumpro
#CPPFLAGS = -m486
CPPFLAGS = -mcpu=i486
LDFLAGS = -L/usr/local/lib -L/usr/lib -L/usr/local/ssl/lib -L$(PPLIB)
LIBS = -lpp -lz -lpthread -lssl -lcrypto -lpq
CPLUS_INCLUDE_PATH = incl/:$(PPLIB):/usr/local/ssl/include/
export CPLUS_INCLUDE_PATH

.cpp.o: $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $*.o
#	ar r $(LIB) $*.o
#	rm -f $*.o

all: Makefile $(PROGS)
.PHONY: all

clean:
	rm -f $(PROGS) *.o core
.PHONY: clean

##########################
# Linking rules.
#
#pokersrv: Makefile $(OBJECTS) /usr/local/lib/libpp.a
pokersrv: Makefile $(OBJECTS) $(PPLIB)/libpp.a 
	$(CC) $(OBJECTS) $(LIBS) $(LDFLAGS) -o $@
	@echo "GCC map file" >pokersrv.map
	@nm pokersrv|sort>>pokersrv.map
	@echo Build complete.

##########################
# Compilation rules.
#
# Take the shortcut and make all object files depend on all header files.
#
$(OBJECTS): $(HEADERS)
