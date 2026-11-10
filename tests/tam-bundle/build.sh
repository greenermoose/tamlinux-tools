#!/usr/bin/env bash
# tam-bundle build and lock tests: lock files, building from exact commits in
# small local repositories, the build cache, reproducible hashes, and the
# refusals. Everything happens under $TAM_TEST_DIR (default $TMPDIR, else
# /tmp); nothing touches the network.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${TAM_BUNDLE_BIN:-$HERE/../../bin/tam-bundle-test}
[ -x "$BIN" ] || { echo "tests/tam-bundle: $BIN is missing; run make bin/tam-bundle-test" >&2; exit 1; }
command -v git >/dev/null && command -v make >/dev/null || { echo "tests/tam-bundle/build.sh: needs git and make" >&2; exit 1; }

BASE=$(mktemp -d "${TAM_TEST_DIR:-${TMPDIR:-/tmp}}/tam-bundle-build.XXXXXX")
trap 'chmod -R u+w "$BASE" 2>/dev/null; rm -rf "$BASE"' EXIT
echo "Running tam-bundle build tests in $BASE..."

FAILS=0
pass() { echo "  [PASS] $*"; }
fail() { echo "  [FAIL] $*" >&2; FAILS=$((FAILS + 1)); }
expect() {
    local want=$1 what=$2
    shift 2
    "$@" >"$BASE/out" 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then pass "$what"; else
        fail "$what (exit $got, wanted $want)"; sed 's/^/        /' "$BASE/out" >&2
    fi
}

# Git and builds see none of the user's own settings.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid
export XDG_CACHE_HOME=$BASE/cache
SLOT=$BASE/slot
STATE=$BASE/state
REPOS=$BASE/repos
LOCK=$BASE/tamlinux.lock
tb() { "$BIN" --slot "$SLOT" --state "$STATE" "$@"; }

commit_all() { # commit_all REPO MESSAGE DATE
    git -C "$1" add -A && GIT_AUTHOR_DATE=$3 GIT_COMMITTER_DATE=$3 git -C "$1" commit -q -m "$2"
    git -C "$1" rev-parse HEAD
}
id_of() { local d=$1 n v h
    n=$(sed -n 's/^name //p' "$d/BUNDLE"); v=$(sed -n 's/^version //p' "$d/BUNDLE")
    h=$(sed -n 's/^hash sha256://p' "$d/BUNDLE"); echo "$n-$v-${h:0:12}"; }

# ---- Source repositories ---------------------------------------------------

# lib: build-only; installs a greeting into its private prefix.
mkdir -p "$REPOS/lib" "$REPOS/app" "$REPOS/bad"
git -C "$REPOS/lib" init -q -b main
printf 'PREFIX ?= /usr/local\nall:\n\t@true\ninstall:\n\tinstall -d $(DESTDIR)$(PREFIX)/share/lib\n\tinstall -m 644 greeting $(DESTDIR)$(PREFIX)/share/lib/greeting\n' > "$REPOS/lib/Makefile"
cat >> "$REPOS/lib/Makefile" <<'EOF'
display session:
	@echo built-$@ > $@
install-display:
	install -m 644 display $(DESTDIR)$(PREFIX)/share/lib/display
install-session:
	install -m 644 session $(DESTDIR)$(PREFIX)/share/lib/session
EOF
echo "hello from lib v1" > "$REPOS/lib/greeting"
LIB1=$(commit_all "$REPOS/lib" "lib v1" "2026-01-01T00:00:00Z")

# app: reads lib's greeting at build time through @lib@, records its prefix,
# SOURCE_DATE_EPOCH and the paths it was built at (so the same-hash checks
# below fail if a cache path leaks in), and installs a command, data and a
# link.
git -C "$REPOS/app" init -q -b main
cat > "$REPOS/app/Makefile" <<'EOF'
PREFIX ?= /usr/local
all: app
app: app.in
	sed -e "s|@GREETING@|$$(cat $(LIB_PREFIX)/share/lib/greeting)|" -e "s|@PREFIX@|$(PREFIX)|" app.in > app
install: app
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/app
	install -m 755 app $(DESTDIR)$(PREFIX)/bin/app
	install -m 644 data $(DESTDIR)$(PREFIX)/share/app/data
	echo "$$SOURCE_DATE_EPOCH $$LC_ALL $$TZ" > $(DESTDIR)$(PREFIX)/share/app/env
	ln -sf ../../bin/app $(DESTDIR)$(PREFIX)/share/app/app-link
	echo "$(CURDIR) $(LIB_PREFIX) $$(touch $(LIB_PREFIX)/probe 2>/dev/null && echo writable || echo read-only)" > $(DESTDIR)$(PREFIX)/share/app/paths
EOF
printf '#!/bin/sh\necho "@GREETING@ in @PREFIX@"\n' > "$REPOS/app/app.in"
echo "app data" > "$REPOS/app/data"
APP1=$(commit_all "$REPOS/app" "app v1" "2026-02-01T00:00:00Z")
APP1_TIME=$(git -C "$REPOS/app" show -s --format=%ct "$APP1")

# bad: misbehaves as MODE says.
git -C "$REPOS/bad" init -q -b main
cat > "$REPOS/bad/Makefile" <<'EOF'
PREFIX ?= /usr/local
all:
	@if [ "$(MODE)" = fail ]; then echo "bad: failing on purpose"; exit 1; fi
install:
	if [ "$(MODE)" = outside ]; then install -d $(DESTDIR)/etc && echo x > $(DESTDIR)/etc/bad; fi
	if [ "$(MODE)" = clash ]; then install -d $(DESTDIR)$(PREFIX)/bin && echo x > $(DESTDIR)$(PREFIX)/bin/app; fi
	if [ "$(MODE)" = setuid ]; then install -d $(DESTDIR)$(PREFIX)/bin && install -m 4755 /dev/null $(DESTDIR)$(PREFIX)/bin/su-bad; fi
EOF
BAD=$(commit_all "$REPOS/bad" "bad" "2026-03-01T00:00:00Z")

# ---- Recipes ----------------------------------------------------------------

RECIPES=$BASE/recipes
mkdir -p "$RECIPES"
printf 'format 1\nname demo\nneeds command sh\nafter qml-cache-purge\n' > "$RECIPES/bundle"
printf 'format 1\ncomponent lib\nbuild make\noutput build-only\n' > "$RECIPES/lib.recipe"
printf 'format 1\n# app reads lib at build time\ncomponent app\nbuild make\nmake-vars LIB_PREFIX=@lib@\nafter-build lib\n' > "$RECIPES/app.recipe"

# variant NAME: a copy of the recipes to change for one refusal test.
variant() { rm -rf "$BASE/r-$1"; cp -a "$RECIPES" "$BASE/r-$1"; echo "$BASE/r-$1"; }

# ---- lock ------------------------------------------------------------------

expect 1 "lock refuses a new component without --url" tb lock lib "$LIB1" --lock "$LOCK"
expect 0 "lock starts a lock file with --url" tb lock lib "$LIB1" --lock "$LOCK" --url "$REPOS/lib"
sed -i '1a # pinned by hand' "$LOCK"
expect 2 "lock refuses a short revision without --repos" tb lock app HEAD --lock "$LOCK" --url "$REPOS/app"
expect 0 "lock resolves a revision in a local clone" tb lock app main --lock "$LOCK" --url "$REPOS/app" --repos "$REPOS"
grep -q "^component app $REPOS/app $APP1\$" "$LOCK" && pass "the resolved commit is the full id" \
    || fail "lock line for app: $(cat "$LOCK")"
grep -q '^# pinned by hand$' "$LOCK" && pass "lock keeps the rest of the file" || fail "comment lost: $(cat "$LOCK")"
expect 0 "lock with the same commit changes nothing" tb lock lib "$LIB1" --lock "$LOCK"
grep -q "stays at" "$BASE/out" && pass "lock says the commit stays" || fail "unchanged message: $(cat "$BASE/out")"
expect 1 "lock refuses a name that is no commit" tb lock app no-such-branch --lock "$LOCK" --repos "$REPOS"
cp "$LOCK" "$BASE/lock.before"
expect 0 "lock --dry-run" tb --dry-run lock lib 0000000000000000000000000000000000000000 --lock "$LOCK"
cmp -s "$LOCK" "$BASE/lock.before" && pass "--dry-run leaves the lock file alone" || fail "dry run changed the lock"

# ---- build -----------------------------------------------------------------

expect 0 "build a bundle from the lock" tb build "$RECIPES" "$LOCK" -o "$BASE/b1" --version 1.0
expect 0 "the built bundle verifies" tb verify "$BASE/b1"
B1=$(id_of "$BASE/b1")
grep -q "^source lib $LIB1\$" "$BASE/b1/BUNDLE" && grep -q "^source app $APP1\$" "$BASE/b1/BUNDLE" \
    && pass "BUNDLE records every component's commit" || fail "sources: $(cat "$BASE/b1/BUNDLE")"
grep -q '^tool make ' "$BASE/b1/BUNDLE" && grep -q '^needs command sh$' "$BASE/b1/BUNDLE" \
    && grep -q '^after qml-cache-purge$' "$BASE/b1/BUNDLE" \
    && pass "BUNDLE carries tools, needs and steps" || fail "BUNDLE: $(cat "$BASE/b1/BUNDLE")"
[ -x "$BASE/b1/bin/app" ] && [ -L "$BASE/b1/share/app/app-link" ] && [ ! -e "$BASE/b1/share/lib" ] \
    && pass "app's files are in the bundle and build-only lib's are not" || fail "bundle tree: $(cd "$BASE/b1" && find . | sort)"
[ "$(cat "$BASE/b1/share/app/env")" = "$APP1_TIME C UTC" ] \
    && pass "builds see SOURCE_DATE_EPOCH at the commit time, LC_ALL=C and TZ=UTC" \
    || fail "build environment: $(cat "$BASE/b1/share/app/env")"
[ "$(cat "$BASE/b1/share/app/paths")" = "/tmp/tam-bundle/work/src /tmp/tam-bundle/deps/lib read-only" ] \
    && pass "builds run at fixed paths, with build-only dependencies read-only" \
    || fail "build paths: $(cat "$BASE/b1/share/app/paths")"
[ -z "$(git -C "$REPOS/app" ls-files -o)" ] && pass "the build left the source repository alone" || fail "repo changed"
[ -d "$XDG_CACHE_HOME/tamlinux/tam-bundle/git/app.git" ] && pass "sources came through the cache's mirror" \
    || fail "no mirror for app"

expect 1 "build refuses an output that exists" tb build "$RECIPES" "$LOCK" -o "$BASE/b1" --version 1.0
expect 0 "build again from the cache" tb build "$RECIPES" "$LOCK" -o "$BASE/b2" --version 1.0
[ "$(grep -c 'from the build cache' "$BASE/out")" = 2 ] && pass "both components came from the cache" \
    || fail "cache use: $(cat "$BASE/out")"
[ "$(id_of "$BASE/b2")" = "$B1" ] && pass "the cached build has the same hash" || fail "cached hash differs"
XDG_CACHE_HOME=$BASE/cache2 expect 0 "build with an empty cache from local clones" \
    tb build "$RECIPES" "$LOCK" -o "$BASE/b3" --version 1.0 --repos "$REPOS"
[ "$(id_of "$BASE/b3")" = "$B1" ] && pass "building the same lock twice gives the same hash" \
    || fail "rebuild hash differs: $(id_of "$BASE/b3") vs $B1"
[ ! -d "$BASE/cache2/tamlinux/tam-bundle/git/app.git" ] && pass "--repos used the local clone, no mirror" \
    || fail "--repos still made a mirror"

expect 0 "switch the built bundle into its slot" tb --quiet switch "$BASE/b1"
[ "$("$SLOT/current/bin/app")" = "hello from lib v1 in $SLOT/current" ] \
    && pass "the live command was built for <slot>/current with lib's data" \
    || fail "app prints: $("$SLOT/current/bin/app")"

# A new lib commit rebuilds app, which was built against it.
echo "hello from lib v2" > "$REPOS/lib/greeting"
LIB2=$(commit_all "$REPOS/lib" "lib v2" "2026-04-01T00:00:00Z")
tb --quiet lock lib "$LIB2" --lock "$LOCK"
expect 0 "build after a dependency changed" tb build "$RECIPES" "$LOCK" -o "$BASE/b4" --version 1.1
grep -q '^building app' "$BASE/out" && pass "a changed after-build component rebuilds its dependants" \
    || fail "app not rebuilt: $(cat "$BASE/out")"
grep -q 'lib v2' "$BASE/b4/bin/app" && pass "the rebuilt command has the new data" || fail "b4 app"
expect 0 "switch to the rebuilt bundle" tb --quiet switch "$BASE/b4"
expect 0 "and back to the first build" tb --quiet back
[ "$("$SLOT/current/bin/app")" = "hello from lib v1 in $SLOT/current" ] && pass "rollback restores the first build" \
    || fail "after back: $("$SLOT/current/bin/app")"

# ---- Explicit make targets and packaging files ------------------------------

R=$(variant targets)
printf 'make-targets all display session\ninstall-targets install install-display install-session\n' >> "$R/lib.recipe"
printf 'make-vars EXTRA=yes\n' >> "$R/app.recipe"
expect 0 "build with several build and install targets" tb build "$R" "$LOCK" -o "$BASE/targets" --version 2
grep -rlq 'built-display' "$XDG_CACHE_HOME/tamlinux/tam-bundle/build/"*/out/share/lib/display \
    && grep -rlq 'built-session' "$XDG_CACHE_HOME/tamlinux/tam-bundle/build/"*/out/share/lib/session \
    && pass "both additional library targets were installed in the private prefix" || fail "missing additional library installs"
for badtarget in '--eval=bad' 'PREFIX=/outside' 'all;false' '-f/etc/passwd'; do
    R=$(variant targetbad)
    printf 'make-targets %s\n' "$badtarget" >> "$R/app.recipe"
    expect 1 "refuse make target $badtarget" tb build "$R" "$LOCK" -o "$BASE/targetbad" --version 2
    sed -i 's/^make-targets /install-targets /' "$R/app.recipe"
    expect 1 "refuse install target $badtarget" tb build "$R" "$LOCK" -o "$BASE/targetbad" --version 2
done

PKG=$BASE/packaging
mkdir -p "$PKG/notes"
cp -a "$RECIPES" "$PKG/recipes"
cp "$LOCK" "$PKG/tamlinux.lock"
printf 'Version notes from the pinned packaging commit.\n' > "$PKG/notes/version.txt"
ln -s version.txt "$PKG/notes/link"
printf 'file notes/version.txt share/tamlinux/version notes.txt\n' >> "$PKG/recipes/bundle"
git -C "$PKG" init -q -b main
PACK1=$(commit_all "$PKG" "packaging" "2026-04-02T00:00:00Z")
pb() { tb build "$PKG/recipes" "$PKG/tamlinux.lock" -o "$1" --version 2 --repos "$REPOS"; }
expect 0 "build packaging files from a recorded commit" pb "$BASE/files1"
cmp -s "$PKG/notes/version.txt" "$BASE/files1/share/tamlinux/version notes.txt" \
    && pass "file entries support spaces in the destination and create parents" || fail "notes differ"
grep -q "^source packaging $PACK1$" "$BASE/files1/BUNDLE" \
    && pass "BUNDLE records the packaging commit" || fail "packaging source missing"
expect 0 "bundle containing packaging files verifies" tb verify "$BASE/files1"
echo 'Uncommitted notes must not enter the bundle.' > "$PKG/notes/version.txt"
expect 0 "extra files come from the commit even when the working copy changes" pb "$BASE/files2"
[ "$(id_of "$BASE/files1")" = "$(id_of "$BASE/files2")" ] \
    && pass "dirty notes cannot change the recorded bundle" || fail "dirty notes changed hash"
printf '# dirty recipe\n' >> "$PKG/recipes/app.recipe"
expect 1 "refuse recipes different from the packaging commit" pb "$BASE/files-dirty"
PACK2=$(commit_all "$PKG" "updated notes" "2026-04-03T00:00:00Z")
expect 0 "build committed updated notes" pb "$BASE/files3"
[ "$(id_of "$BASE/files1")" != "$(id_of "$BASE/files3")" ] \
    && grep -q "^source packaging $PACK2$" "$BASE/files3/BUNDLE" \
    && pass "packaging changes give a new recorded bundle" || fail "packaging update not recorded"
for pair in 'notes/missing share/missing' 'notes/link share/link' 'notes/version.txt bin/app' \
            'notes/version.txt share/app/app-link/child' 'notes/version.txt BUNDLE' \
            '../outside share/outside' '/etc/passwd share/outside' 'notes/version.txt ../outside'; do
    printf 'format 1\nname demo\nfile %s\n' "$pair" > "$PKG/recipes/bundle"
    commit_all "$PKG" "invalid file entry" "2026-04-04T00:00:00Z" >/dev/null
    expect 1 "refuse file entry: $pair" pb "$BASE/files-invalid"
    [ ! -e "$BASE/files-invalid" ] && [ ! -e "$BASE/files-invalid.partial" ] \
        && pass "invalid file left no bundle" || fail "invalid file left output"
done
R=$(variant files-no-repo)
printf 'file notes/version.txt share/notes\n' >> "$R/bundle"
expect 1 "file entries require a packaging repository" tb build "$R" "$LOCK" -o "$BASE/files-no-repo" --version 2

# ---- Manual index -------------------------------------------------------------

# doc installs two manual pages and a link to one; MODE=index also installs
# an index of its own, which build refuses.
mkdir -p "$REPOS/doc"
git -C "$REPOS/doc" init -q -b main
cat > "$REPOS/doc/Makefile" <<'EOF_DOC'
PREFIX ?= /usr/local
all:
	@true
install:
	install -d $(DESTDIR)$(PREFIX)/share/man/man1 $(DESTDIR)$(PREFIX)/share/man/man5
	install -m 644 demo-tool.1 $(DESTDIR)$(PREFIX)/share/man/man1/demo-tool.1
	install -m 644 demo.conf.5 $(DESTDIR)$(PREFIX)/share/man/man5/demo.conf.5
	ln -sf demo-tool.1 $(DESTDIR)$(PREFIX)/share/man/man1/demo-alias.1
	if [ "$(MODE)" = index ]; then echo x > $(DESTDIR)$(PREFIX)/share/man/index.db; fi
EOF_DOC
printf '.TH DEMO-TOOL 1\n.SH NAME\ndemo-tool \\- run the demo tool\n.SH DESCRIPTION\nDemo.\n' > "$REPOS/doc/demo-tool.1"
printf '.TH DEMO.CONF 5\n.SH NAME\ndemo.conf \\- the demo settings file\n.SH DESCRIPTION\nDemo.\n' > "$REPOS/doc/demo.conf.5"
DOC=$(commit_all "$REPOS/doc" "doc" "2026-05-01T00:00:00Z")
DOC_TIME=$(git -C "$REPOS/doc" show -s --format=%ct "$DOC")
cp "$LOCK" "$BASE/lock-doc"
tb --quiet lock doc "$DOC" --lock "$BASE/lock-doc" --url "$REPOS/doc"
DOCR=$(variant doc); printf 'format 1\ncomponent doc\nbuild make\n' > "$DOCR/doc.recipe"
IDXR=$(variant index); cp "$DOCR/doc.recipe" "$IDXR/"; printf 'index man\n' >> "$IDXR/bundle"

if command -v mandb >/dev/null && command -v whatis >/dev/null && command -v apropos >/dev/null; then
    expect 0 "build with the doc component, no index" tb build "$DOCR" "$BASE/lock-doc" -o "$BASE/m0" --version 2.0
    [ ! -e "$BASE/m0/share/man/index.db" ] && pass "without \`index man\` the bundle has no manual index" \
        || fail "m0 has an index"
    expect 0 "build with \`index man\`" tb build "$IDXR" "$BASE/lock-doc" -o "$BASE/m1" --version 2.0
    [ "$(grep -c 'from the build cache' "$BASE/out")" = 3 ] \
        && pass "adding the index rebuilds no component" || fail "index rebuilt components: $(cat "$BASE/out")"
    grep -q '^f 0644 sha256:[0-9a-f]* *share/man/index.db$' "$BASE/m1/MANIFEST" \
        && pass "share/man/index.db is in the MANIFEST, hashed like any file" \
        || fail "MANIFEST: $(grep share/man "$BASE/m1/MANIFEST")"
    grep -q '^tool man-db [0-9]' "$BASE/m1/BUNDLE" && pass "BUNDLE records the man-db version" \
        || fail "BUNDLE tools: $(grep '^tool' "$BASE/m1/BUNDLE")"
    [ "$(ls -A "$BASE/m1/share/man")" = "$(printf 'index.db\nman1\nman5')" ] \
        && pass "mandb added only index.db" || fail "share/man holds: $(ls -A "$BASE/m1/share/man")"
    grep -aq "$DOC_TIME" "$BASE/m1/share/man/index.db" \
        && pass "the index records the newest locked commit's time" || fail "index time"
    M1=$(id_of "$BASE/m1")
    XDG_CACHE_HOME=$BASE/cache3 expect 0 "build the index again with an empty cache" \
        tb build "$IDXR" "$BASE/lock-doc" -o "$BASE/m2" --version 2.0 --repos "$REPOS"
    [ "$(id_of "$BASE/m2")" = "$M1" ] && cmp -s "$BASE/m1/share/man/index.db" "$BASE/m2/share/man/index.db" \
        && pass "the same lock gives the same index and the same hash" \
        || fail "index rebuild differs: $(id_of "$BASE/m2") vs $M1"

    expect 0 "switch the indexed bundle into its slot" tb --quiet switch "$BASE/m1"
    chmod -R a-w "$SLOT/current/share/man"
    mt() { env -i PATH="$PATH" HOME="$BASE/home" MANPATH="$SLOT/current/share/man" "$@" 2>&1; }
    mt whatis demo-tool | grep -q 'run the demo tool' && mt whatis demo-alias | grep -q 'run the demo tool' \
        && pass "whatis finds the pages, and the link, from the live tree" || fail "whatis: $(mt whatis demo-tool)"
    mt apropos -s 5 . | grep -q '^demo.conf (5)' && pass "apropos lists a section from the live tree" \
        || fail "apropos: $(mt apropos -s 5 .)"
    chmod -R u+w "$SLOT/current/share/man"
    expect 0 "the live tree still matches after using the index" tb check --full
    [ ! -e "$BASE/home" ] && pass "the manual tools wrote nothing in the home folder" || fail "home: $(find "$BASE/home")"
    expect 0 "back to the bundle without manual pages" tb --quiet back
    [ ! -e "$SLOT/current/share/man" ] && pass "rollback takes the index away with its pages" || fail "index left after back"

    R=$(variant noman); printf 'index man\n' >> "$R/bundle"
    expect 1 "build refuses \`index man\` for a bundle with no share/man" tb build "$R" "$LOCK" -o "$BASE/x-noman" --version 9
    grep -q 'no share/man' "$BASE/out" && pass "  ...and says why" || fail "noman message: $(cat "$BASE/out")"
    R=$(variant ownidx); printf 'format 1\ncomponent doc\nbuild make\nmake-vars MODE=index\n' > "$R/doc.recipe"
    printf 'index man\n' >> "$R/bundle"
    expect 1 "build refuses a component that installs its own manual index" \
        tb build "$R" "$BASE/lock-doc" -o "$BASE/x-ownidx" --version 9
    no_output() { [ ! -e "$1" ] && [ ! -e "$1.partial" ]; }
    no_output "$BASE/x-ownidx" && pass "  ...and leaves no bundle behind" || fail "ownidx left output"
else
    echo "  [SKIP] man-db is not installed; manual index not tested"
fi
R=$(variant idxkind); printf 'index info\n' >> "$R/bundle"
expect 1 "build refuses an index of anything but man" tb build "$R" "$LOCK" -o "$BASE/x-idxkind" --version 9
R=$(variant oldstep); printf 'after man-index\n' >> "$R/bundle"
expect 1 "man-index is no longer a post-install step" tb build "$R" "$LOCK" -o "$BASE/x-oldstep" --version 9

# ---- Refusals ----------------------------------------------------------------

no_output() { [ ! -e "$1" ] && [ ! -e "$1.partial" ]; }
cp "$LOCK" "$BASE/lock-bad"
tb --quiet lock bad "$BAD" --lock "$BASE/lock-bad" --url "$REPOS/bad"
for mode in outside clash fail setuid; do
    R=$(variant "$mode")
    printf 'format 1\ncomponent bad\nbuild make\nmake-vars MODE=%s\nafter-build app\n' "$mode" > "$R/bad.recipe"
    expect 1 "build refuses a component that does '$mode'" tb build "$R" "$BASE/lock-bad" -o "$BASE/x-$mode" --version 9
    no_output "$BASE/x-$mode" && pass "  ...and leaves no bundle behind" || fail "$mode left output"
done
R=$(variant fail2); printf 'format 1\ncomponent bad\nbuild make\nmake-vars MODE=fail\n' > "$R/bad.recipe"
tb build "$R" "$BASE/lock-bad" -o "$BASE/x-fail2" --version 9 >"$BASE/out" 2>&1
grep -q 'bad: failing on purpose' "$BASE/out" && pass "a failed make shows the end of its log" \
    || fail "failure output: $(cat "$BASE/out")"

R=$(variant cycle); printf 'after-build app\n' >> "$R/lib.recipe"
expect 1 "build refuses an after-build cycle" tb build "$R" "$LOCK" -o "$BASE/x-cycle" --version 9
R=$(variant prefix); printf 'make-vars PREFIX=/elsewhere\n' >> "$R/app.recipe"
expect 1 "build refuses a recipe that sets PREFIX" tb build "$R" "$LOCK" -o "$BASE/x-prefix" --version 9
R=$(variant ref); sed -i 's/^after-build lib$//' "$R/app.recipe"
expect 1 "build refuses @lib@ without after-build lib" tb build "$R" "$LOCK" -o "$BASE/x-ref" --version 9
R=$(variant kind); sed -i 's/^build make$/build cmake/' "$R/app.recipe"
expect 1 "build refuses an unknown build kind" tb build "$R" "$LOCK" -o "$BASE/x-kind" --version 9
R=$(variant name); sed -i 's/^component app$/component other/' "$R/app.recipe"
expect 1 "build refuses a recipe whose component is not its file name" tb build "$R" "$LOCK" -o "$BASE/x-name" --version 9
R=$(variant extra); printf 'format 1\ncomponent extra\nbuild make\n' > "$R/extra.recipe"
expect 1 "build refuses a recipe the lock does not pin" tb build "$R" "$LOCK" -o "$BASE/x-extra" --version 9
expect 1 "build refuses a lock line with no recipe" tb build "$RECIPES" "$BASE/lock-bad" -o "$BASE/x-stale" --version 9
sed 's/^\(component lib [^ ]*\) .*/\1 1234567/' "$LOCK" > "$BASE/lock-short"
expect 1 "build refuses a lock with a short commit" tb build "$RECIPES" "$BASE/lock-short" -o "$BASE/x-short" --version 9
sed "s/$LIB2/$(printf '%040d' 0 | tr 0 a)/" "$LOCK" > "$BASE/lock-missing"
expect 1 "build refuses a commit the source does not have" tb build "$RECIPES" "$BASE/lock-missing" -o "$BASE/x-missing" --version 9
expect 2 "build needs --version" tb build "$RECIPES" "$LOCK" -o "$BASE/x-nover"
expect 2 "build needs an absolute slot" "$BIN" --slot rel/slot build "$RECIPES" "$LOCK" -o "$BASE/x-rel" --version 9

if command -v flock >/dev/null; then
    flock "$XDG_CACHE_HOME/tamlinux/tam-bundle/lock" sleep 5 &
    lpid=$!
    sleep 0.3
    expect 1 "a second build refuses while the cache is in use" tb build "$RECIPES" "$LOCK" -o "$BASE/x-locked" --version 9
    kill $lpid 2>/dev/null; wait $lpid 2>/dev/null
fi

if [ $FAILS -eq 0 ]; then echo "All tam-bundle build tests passed."; exit 0; fi
echo "$FAILS tam-bundle build test(s) failed." >&2
exit 1
