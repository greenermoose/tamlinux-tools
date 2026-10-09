CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local
LIBTAM_PREFIX ?= /usr/local

INCLUDES = -I$(LIBTAM_PREFIX)/include $(shell pkg-config --cflags dbus-1 2>/dev/null)
LIBS = -L$(LIBTAM_PREFIX)/lib -Wl,-rpath,$(LIBTAM_PREFIX)/lib -ltam $(shell pkg-config --libs dbus-1 2>/dev/null)

BIN = bin/tam-file-select

.PHONY: all clean test install

all: bin $(BIN)

bin:
	mkdir -p bin

bin/tam-file-select: src/tam-file-select.c | bin
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBS) -o $@

test: all
	@tests/run_tests.sh

clean:
	rm -rf bin

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/
