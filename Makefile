CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local

# tam-file-select uses libtam's desktop portal, the separate static library
# libtam-portal (libtam 0.6.0 or later), found with
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
LIBTAM_ERROR  := $(if $(and $(wildcard $(LIBTAM_PREFIX)/include/tam_portal.h),$(shell grep -s -l tam_install_pending $(LIBTAM_PREFIX)/include/tam.h)),,LIBTAM_PREFIX=$(LIBTAM_PREFIX) has no include/tam_portal.h and libtam 0.8.0 or later)
else
LIBTAM_FOUND  := $(shell $(PKG_CONFIG) --atleast-version=0.8.0 libtam-portal 2>/dev/null && echo yes)
LIBTAM_CFLAGS := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --cflags libtam-portal))
LIBTAM_LIBS   := $(if $(LIBTAM_FOUND),$(shell $(PKG_CONFIG) --libs libtam-portal))
LIBTAM_ERROR  := $(if $(LIBTAM_FOUND),,$(PKG_CONFIG) cannot find libtam-portal (libtam 0.8.0 or later))
endif

INCLUDES = $(LIBTAM_CFLAGS)
LIBS = $(LIBTAM_LIBS)

# tam-journal needs only the libtam core, 0.8.0 or later.
ifdef LIBTAM_PREFIX
CORE_CFLAGS := -I$(LIBTAM_PREFIX)/include
CORE_LIBS   := -L$(LIBTAM_PREFIX)/lib -ltam
CORE_ERROR  := $(if $(shell grep -s -l tam_install_pending $(LIBTAM_PREFIX)/include/tam.h),,LIBTAM_PREFIX=$(LIBTAM_PREFIX) has no libtam 0.8.0 or later include/tam.h)
else
CORE_FOUND  := $(shell $(PKG_CONFIG) --atleast-version=0.8.0 libtam 2>/dev/null && echo yes)
CORE_CFLAGS := $(if $(CORE_FOUND),$(shell $(PKG_CONFIG) --cflags libtam))
CORE_LIBS   := $(if $(CORE_FOUND),$(shell $(PKG_CONFIG) --libs libtam))
CORE_ERROR  := $(if $(CORE_FOUND),,$(PKG_CONFIG) cannot find libtam 0.8.0 or later)
endif

LIBTAM_DIR := $(if $(LIBTAM_PREFIX),$(LIBTAM_PREFIX)/lib,$(shell $(PKG_CONFIG) --variable=libdir libtam 2>/dev/null))
CORE_ARCHIVE := $(wildcard $(LIBTAM_DIR)/libtam.a)
PORTAL_ARCHIVE := $(wildcard $(LIBTAM_DIR)/libtam-portal.a)

BIN = bin/tam-file-select bin/tam-journal

# libtam is linked statically, so the binary carries the libtam it was built
# with. The stamp changes with that configuration and forces a rebuild.
CONFIG := $(CC) $(CFLAGS) $(INCLUDES) $(LIBS) $(CORE_CFLAGS) $(CORE_LIBS)

.PHONY: all clean test install install-file-select install-journal test-journal uninstall libtam-check core-check FORCE

all: bin $(BIN)

bin:
	mkdir -p bin

bin/.config: FORCE | bin
	@printf '%s\n' '$(CONFIG)' | cmp -s - $@ || printf '%s\n' '$(CONFIG)' > $@

bin/tam-file-select: src/tam-file-select.c bin/.config $(CORE_ARCHIVE) $(PORTAL_ARCHIVE) | bin libtam-check
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBS) -o $@

bin/tam-journal: src/tam-journal.c bin/.config $(CORE_ARCHIVE) | bin core-check
	$(CC) $(CFLAGS) -std=c11 -g $(CORE_CFLAGS) $< $(CORE_LIBS) -o $@

test-journal: bin/tam-journal
	python3 -B tests/journal/test_journal.py

core-check:
ifneq ($(CORE_ERROR),)
	@printf '%s\n' 'tamlinux-tools: cannot build tam-journal: $(CORE_ERROR).' \
		'  Install libtam 0.8.0 or later first:   make -C <libtam checkout> install PREFIX=<prefix>' \
		'  then let pkg-config see it:   export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig' >&2
	@exit 1
endif

# Stop before the compiler does, with what to do instead of a missing tam.h.
libtam-check:
ifneq ($(LIBTAM_ERROR),)
	@printf '%s\n' 'tamlinux-tools: cannot build: $(LIBTAM_ERROR).' \
		'  Install libtam 0.8.0 or later first:   make -C <libtam checkout> install PREFIX=<prefix>' \
		'  then let pkg-config see it:   export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig' \
		'  or name the prefix:   make LIBTAM_PREFIX=<prefix>' >&2
	@exit 1
endif

test: all test-journal
	@tests/run_tests.sh
	@tests/man.sh

clean:
	rm -rf bin

MANDIR ?= $(PREFIX)/share/man

install: install-file-select install-journal

install-file-select: bin/tam-file-select
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 bin/tam-file-select $(DESTDIR)$(PREFIX)/bin/

install-journal: bin/tam-journal
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MANDIR)/man1
	install -m 755 bin/tam-journal $(DESTDIR)$(PREFIX)/bin/
	install -m 644 man/tam-journal.1 $(DESTDIR)$(MANDIR)/man1/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-file-select $(DESTDIR)$(PREFIX)/bin/tam-journal
	rm -f $(DESTDIR)$(MANDIR)/man1/tam-journal.1
