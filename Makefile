CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=500
LDFLAGS =
LIBS = -lcrypto
PREFIX ?= /usr/local

all: fargs

fargs: fargs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

install: fargs
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 fargs $(DESTDIR)$(PREFIX)/bin
	install -m 0644 fargs.1 $(DESTDIR)$(PREFIX)/share/man/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fargs

clean:
	rm -f fargs
