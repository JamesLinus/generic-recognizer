CC=gcc
CFLAGS=-c -g -Wall -Wconversion -Wno-switch -Wno-parentheses -Wno-sign-conversion

all: genrec

genrec: genrec.o lex.o util.o
	$(CC) -o genrec genrec.o lex.o util.o

genrec.o: genrec.c lex.h
	$(CC) $(CFLAGS) genrec.c

lex.o: lex.c lex.h tokens.def
	$(CC) $(CFLAGS) lex.c

util.o: util.c util.h
	$(CC) $(CFLAGS) util.c

clean:
	rm -f *.o genrec

.PHONY: all clean
