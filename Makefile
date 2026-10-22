CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local

# tam-file-select uses libtam's desktop portal, the separate static library
# libtam-portal (libtam 0.4.0 or later), found with
# `pkg-config libtam-portal`, which adds libtam and D-Bus. Add
# <prefix>/lib/pkgconfig to PKG_CONFIG_PATH for a prefix outside
# pkg-config's default path, or set LIBTAM_PREFIX to name the install prefix
# instead.
PKG_CONFIG ?= pkg-config
DBUS_CFLAGS := $(shell $(PKG_CONFIG) --cflags dbus-1 2>/dev/null)
DBUS_LIBS   := $(shell $(PKG_CONFIG) --libs dbus-1 2>/dev/null)
ifdef LIBTAM_PREFIX
LIBTAM_CFLAGS := -I$(LIBTAM_PREFIX)/include $(DBUS_CFLAGS)
LIBTAM_LIBS   := -L$(LIBTAM_PREFIX)/lib -ltam-portal -ltam $(DBUS_LIBS)
LIBTAM_ERROR  := $(if $(wildcard $(LIBTAM_PREFIX)/include/tam_portal.h),,LIBTAM_PREFIX=$(LIBTAM_PREFIX) has no include/tam_portal.h (libtam 0.4.0 or later))
else
LIBTAM_FOUND  := $(shell $(PKG_CONFIG) --exists libtam-portal 2>/dev/null && echo yes)
LIBTAM_CFLAGS := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --cflags libtam-portal))
LIBTAM_LIBS   := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --libs libtam-portal))
LIBTAM_ERROR  := $(if $(LIBTAM_FOUND),,$(PKG_CONFIG) cannot find libtam-portal (libtam 0.4.0 or later))
endif

INCLUDES = $(LIBTAM_CFLAGS)
LIBS = $(LIBTAM_LIBS)

BIN = bin/tam-file-select

# libtam is linked statically, so the binary carries the libtam it was built
# with. The stamp changes with that configuration and forces a rebuild.
CONFIG := $(CC) $(CFLAGS) $(INCLUDES) $(LIBS)

.PHONY: all clean test install uninstall libtam-check FORCE

all: bin $(BIN)

bin:
	mkdir -p bin

bin/.config: FORCE | bin
	@printf '%s\n' '$(CONFIG)' | cmp -s - $@ || printf '%s\n' '$(CONFIG)' > $@

bin/tam-file-select: src/tam-file-select.c bin/.config | bin libtam-check
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBS) -o $@

# Stop before the compiler does, with what to do instead of a missing tam.h.
libtam-check:
ifneq ($(LIBTAM_ERROR),)
	@printf '%s\n' 'tamlinux-tools: cannot build: $(LIBTAM_ERROR).' \
		'  Install libtam 0.4.0 or later first:   make -C <libtam checkout> install PREFIX=<prefix>' \
		'  then let pkg-config see it:   export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig' \
		'  or name the prefix:   make LIBTAM_PREFIX=<prefix>' >&2
	@exit 1
endif

test: all
	@tests/run_tests.sh

clean:
	rm -rf bin

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-file-select
