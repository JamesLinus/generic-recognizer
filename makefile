CC=gcc
CFLAGS=-c -g -Wall -Wconversion -Wno-switch -Wno-parentheses -Wno-sign-conversion

all: genrec

genrec: genrec.o lex.o
	$(CC) -o genrec genrec.o lex.o

genpar.o: genrec.c lex.h
	$(CC) $(CFLAGS) genrec.c

lex.o: lex.c lex.h tokens.h
	$(CC) $(CFLAGS) lex.c

clean:
	rm -f *.o genrec

.PHONY: all clean
