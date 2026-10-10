CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local
LIBTAM_PREFIX ?= $(PREFIX)

INCLUDES = -I$(LIBTAM_PREFIX)/include $(shell pkg-config --cflags dbus-1 2>/dev/null)
LIBS = -L$(LIBTAM_PREFIX)/lib -Wl,-rpath,$(LIBTAM_PREFIX)/lib -ltam $(shell pkg-config --libs dbus-1 2>/dev/null)

BIN = bin/tam-file-select

# The binary embeds the libtam rpath, so a build made for one prefix must not
# be installed under another. The stamp changes with that configuration and
# forces a rebuild.
CONFIG := $(CC) $(CFLAGS) $(INCLUDES) $(LIBS)

.PHONY: all clean test install uninstall FORCE

all: bin $(BIN)

bin:
	mkdir -p bin

bin/.config: FORCE | bin
	@printf '%s\n' '$(CONFIG)' | cmp -s - $@ || printf '%s\n' '$(CONFIG)' > $@

bin/tam-file-select: src/tam-file-select.c bin/.config | bin
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBS) -o $@

test: all
	@tests/run_tests.sh

clean:
	rm -rf bin

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-file-select
