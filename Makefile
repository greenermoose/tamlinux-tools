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

# tam-bundle needs only the libtam core, 0.5.0 or later (SHA-256, the tree
# walk, record files and tree operations).
ifdef LIBTAM_PREFIX
CORE_CFLAGS := -I$(LIBTAM_PREFIX)/include
CORE_LIBS   := -L$(LIBTAM_PREFIX)/lib -ltam
CORE_ERROR  := $(if $(shell grep -s -l tam_fs_exchange $(LIBTAM_PREFIX)/include/tam.h),,LIBTAM_PREFIX=$(LIBTAM_PREFIX) has no libtam 0.5.0 or later include/tam.h)
else
CORE_FOUND  := $(shell $(PKG_CONFIG) --atleast-version=0.5.0 libtam 2>/dev/null && echo yes)
CORE_CFLAGS := $(if $(CORE_FOUND),$(shell $(PKG_CONFIG) --cflags libtam))
CORE_LIBS   := $(if $(CORE_FOUND),$(shell $(PKG_CONFIG) --libs libtam))
CORE_ERROR  := $(if $(CORE_FOUND),,$(PKG_CONFIG) cannot find libtam 0.5.0 or later)
endif

BUNDLE_SRCS := $(wildcard src/tam-bundle/*.c)
BUNDLE_HDRS := $(wildcard src/tam-bundle/*.h)

BIN = bin/tam-file-select bin/tam-bundle

# libtam is linked statically, so the binary carries the libtam it was built
# with. The stamp changes with that configuration and forces a rebuild.
CONFIG := $(CC) $(CFLAGS) $(INCLUDES) $(LIBS) $(CORE_CFLAGS) $(CORE_LIBS)

.PHONY: all clean test install install-file-select install-bundle uninstall libtam-check core-check FORCE

all: bin $(BIN)

bin:
	mkdir -p bin

bin/.config: FORCE | bin
	@printf '%s\n' '$(CONFIG)' | cmp -s - $@ || printf '%s\n' '$(CONFIG)' > $@

bin/tam-file-select: src/tam-file-select.c bin/.config | bin libtam-check
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIBS) -o $@

bin/tam-bundle: $(BUNDLE_SRCS) $(BUNDLE_HDRS) bin/.config | bin core-check
	$(CC) $(CFLAGS) -static $(CORE_CFLAGS) $(BUNDLE_SRCS) $(CORE_LIBS) -o $@

# The test build acts on fault points ($$TAM_BUNDLE_FAULT) and skips
# post-install steps; it is never installed.
bin/tam-bundle-test: $(BUNDLE_SRCS) $(BUNDLE_HDRS) bin/.config | bin core-check
	$(CC) $(CFLAGS) -DTAM_BUNDLE_TESTING -fsanitize=address,undefined $(CORE_CFLAGS) $(BUNDLE_SRCS) $(CORE_LIBS) -o $@

bin/tam-bundle-busy-test: tests/tam-bundle/busy.c src/tam-bundle/busy.c src/tam-bundle/util.c $(BUNDLE_HDRS) bin/.config | bin core-check
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(CORE_CFLAGS) -Isrc/tam-bundle tests/tam-bundle/busy.c src/tam-bundle/busy.c src/tam-bundle/util.c $(CORE_LIBS) -o $@

core-check:
ifneq ($(CORE_ERROR),)
	@printf '%s\n' 'tamlinux-tools: cannot build tam-bundle: $(CORE_ERROR).' \
		'  Install libtam 0.5.0 or later first:   make -C <libtam checkout> install PREFIX=<prefix>' \
		'  then let pkg-config see it:   export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig' >&2
	@exit 1
endif

# Stop before the compiler does, with what to do instead of a missing tam.h.
libtam-check:
ifneq ($(LIBTAM_ERROR),)
	@printf '%s\n' 'tamlinux-tools: cannot build: $(LIBTAM_ERROR).' \
		'  Install libtam 0.4.0 or later first:   make -C <libtam checkout> install PREFIX=<prefix>' \
		'  then let pkg-config see it:   export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig' \
		'  or name the prefix:   make LIBTAM_PREFIX=<prefix>' >&2
	@exit 1
endif

# tests/tam-bundle works in $$TAM_TEST_DIR (default $$TMPDIR, else /tmp); run
# it on Btrfs and ext4 directories to cover both file systems.
test: all bin/tam-bundle-test bin/tam-bundle-busy-test
	@tests/run_tests.sh
	@tests/tam-bundle/run.sh
	@tests/tam-bundle/build.sh
	@python3 -B tests/tam-bundle/build_environment.py
	@python3 -B tests/tam-bundle/build_faults.py
	@python3 -B tests/tam-bundle/namespace.py
	@python3 -B tests/tam-bundle/format2.py
	@python3 -B tests/tam-bundle/settings.py
	@python3 -B tests/tam-bundle/runtime.py
	@tests/man.sh
	@if command -v xmllint >/dev/null; then \
		sed 's|@BINDIR@|/usr/bin|g' data/org.tamlinux.bundle.policy.in | xmllint --noout - \
		&& echo "  [PASS] the polkit action file is well-formed"; \
	else echo "  [SKIP] xmllint is not installed; polkit action file not checked"; fi

clean:
	rm -rf bin

# The polkit action names tam-bundle by its installed path. polkit reads
# actions only from /usr/share/polkit-1/actions, so a development install
# under another prefix carries an inert copy and pkexec asks for an
# administrator through its general action instead.
POLICY = share/polkit-1/actions/org.tamlinux.bundle.policy
MANDIR ?= $(PREFIX)/share/man

install: install-file-select install-bundle

install-file-select: bin/tam-file-select
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 bin/tam-file-select $(DESTDIR)$(PREFIX)/bin/

# Native adapters install the rollback tool outside the product bundle.
install-bundle: bin/tam-bundle
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/polkit-1/actions $(DESTDIR)$(MANDIR)/man1
	install -m 755 bin/tam-bundle $(DESTDIR)$(PREFIX)/bin/
	install -m 644 man/tam-bundle.1 $(DESTDIR)$(MANDIR)/man1/
	sed 's|@BINDIR@|$(PREFIX)/bin|g' data/org.tamlinux.bundle.policy.in > $(DESTDIR)$(PREFIX)/$(POLICY)
	chmod 644 $(DESTDIR)$(PREFIX)/$(POLICY)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-file-select $(DESTDIR)$(PREFIX)/bin/tam-bundle
	rm -f $(DESTDIR)$(PREFIX)/$(POLICY)
	rm -f $(DESTDIR)$(MANDIR)/man1/tam-bundle.1
