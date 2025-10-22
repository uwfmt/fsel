CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS =
LIBS = -lcrypto
PREFIX ?= /usr/local

all: fsel

fsel: fsel.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

install: fsel
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 0755 fsel $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 0644 fsel.1 $(DESTDIR)$(PREFIX)/share/man/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fsel
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/fsel.1

clean:
	rm -f fsel
