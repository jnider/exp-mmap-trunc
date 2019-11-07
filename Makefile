CC=gcc
CFLAGS=-O2 -g
LIBS=-pthread

exp2: LIBS+=-lurcu-mb

.PHONY: file

all: file exp1 exp2

file:
	dd if=/dev/urandom of=large.file count=500

exp1: exp1.c
	$(CC) $(CFLAGS) $(LIBS) exp1.c -o exp1

exp2: exp2.c
	$(CC) $(CFLAGS) -c exp2.c -o exp2.o
	$(CC) exp2.o -o exp2 $(LIBS)
