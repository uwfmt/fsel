CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=500
LDFLAGS =
LIBS = -lcrypto

all: fargs

fargs: fargs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

clean:
	rm -f fargs
