CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local

# libtam is found with `pkg-config libtam`; add <prefix>/lib/pkgconfig to
# PKG_CONFIG_PATH for a prefix outside pkg-config's default path, or set
# LIBTAM_PREFIX to name the install prefix instead.
PKG_CONFIG ?= pkg-config
ifdef LIBTAM_PREFIX
LIBTAM_CFLAGS := -I$(LIBTAM_PREFIX)/include
LIBTAM_LIBS   := -L$(LIBTAM_PREFIX)/lib -ltam
LIBTAM_LIBDIR := $(LIBTAM_PREFIX)/lib
LIBTAM_ERROR  := $(if $(wildcard $(LIBTAM_PREFIX)/include/tam.h),,LIBTAM_PREFIX=$(LIBTAM_PREFIX) has no include/tam.h)
else
LIBTAM_FOUND  := $(shell $(PKG_CONFIG) --exists libtam 2>/dev/null && echo yes)
LIBTAM_CFLAGS := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --cflags libtam))
LIBTAM_LIBS   := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --libs libtam))
LIBTAM_LIBDIR := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --variable=libdir libtam))
LIBTAM_ERROR  := $(if $(LIBTAM_FOUND),,$(PKG_CONFIG) cannot find libtam)
endif

INCLUDES = $(LIBTAM_CFLAGS) $(shell $(PKG_CONFIG) --cflags dbus-1 2>/dev/null)
LIBS = $(LIBTAM_LIBS) $(if $(LIBTAM_LIBDIR),-Wl$(comma)-rpath$(comma)$(LIBTAM_LIBDIR)) $(shell $(PKG_CONFIG) --libs dbus-1 2>/dev/null)
comma := ,

BIN = bin/tam-file-select

# The binary embeds the libtam rpath, so a build made for one prefix must not
# be installed under another. The stamp changes with that configuration and
# forces a rebuild.
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
		'  Install libtam first:   make -C <libtam checkout> install PREFIX=<prefix>' \
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
