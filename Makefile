CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=500
LDFLAGS =
LIBS = -lcrypto
PREFIX ?= /usr/local

all: fsel

fsel: fsel.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

install: fsel
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 fsel $(DESTDIR)$(PREFIX)/bin
	install -m 0644 fsel.1 $(DESTDIR)$(PREFIX)/share/man/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fsel
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/fsel.1

clean:
	rm -f fsel
