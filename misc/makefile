      DIR = .
      OBJ = .
      LIB = $(DIR)/plrlog.a
      PPLIB = ../pplib
      LIBPP = $(PPLIB)/libpp.a
      HPATH = $(PPLIB)/gamedata.h
      IPATH = -I$(DIR) -I$(PPLIB)/pplib
      LFLAG = -L/usr/lib -L/usr/local/lib -L$(PPLIB)

      CC = gcc

      .PRECIOUS: $(OBJ)/plrlog.a

      all: $(OBJ)/plrlog

      $(OBJ)/plrlog: $(LIB)
	ar x $(LIB) plrlog.o
	$(CC) $(LIB) $(LIBPP) $(LFLAG) -lm -lpp -lz -lpthread -lssl -lcrypto -o $@
	rm plrlog.o

      $(DIR)/plrlog.a: $(DIR)/plrlog.o

      .cpp.a: $(HPATH)
	$(CC) -c $(IPATH) $< -o $*.o
	ar r $@ $*.o
	rm -f $*.o
