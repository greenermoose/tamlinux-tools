#!/usr/bin/env bash
# tam-bundle tests: formats, switch and rollback, edits, refusals, the
# interruption (crash) test and the older-bundle test. Everything happens in
# a temporary slot under $TAM_TEST_DIR (default $TMPDIR, else /tmp).
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${TAM_BUNDLE_BIN:-$HERE/../../bin/tam-bundle-test}
[ -x "$BIN" ] || { echo "tests/tam-bundle: $BIN is missing; run make bin/tam-bundle-test" >&2; exit 1; }

BASE=$(mktemp -d "${TAM_TEST_DIR:-${TMPDIR:-/tmp}}/tam-bundle-test.XXXXXX")
trap 'chmod -R u+w "$BASE" 2>/dev/null; rm -rf "$BASE"' EXIT
FS=$(stat -f -c %T "$BASE")
echo "Running tam-bundle tests in $BASE ($FS)..."

FAILS=0
pass() { echo "  [PASS] $*"; }
fail() { echo "  [FAIL] $*" >&2; FAILS=$((FAILS + 1)); }

SLOT=$BASE/slot
STATE=$BASE/state
tb() { "$BIN" --quiet --slot "$SLOT" --state "$STATE" "$@"; }
tbv() { "$BIN" --slot "$SLOT" --state "$STATE" "$@"; }

# expect CODE description command...
expect() {
    local want=$1 what=$2
    shift 2
    "$@" >"$BASE/out" 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then pass "$what"; else
        fail "$what (exit $got, wanted $want)"; sed 's/^/        /' "$BASE/out" >&2
    fi
}

live_id() { sed -n 's/^live: *//p' < <(tbv status 2>/dev/null); }
id_of() { local d=$1; local n v h
    n=$(sed -n 's/^name //p' "$d/BUNDLE"); v=$(sed -n 's/^version //p' "$d/BUNDLE")
    h=$(sed -n 's/^hash sha256://p' "$d/BUNDLE"); echo "$n-$v-${h:0:12}"; }

# make_tree DIR LABEL: 250 small files, a nested tree, a link and a
# read-only directory; LABEL changes a few of them.
make_tree() {
    local d=$1 label=$2
    mkdir -p "$d/bin" "$d/share/many" "$d/share/ro" "$d/share/with space"
    printf '#!/bin/sh\necho %s\n' "$label" > "$d/bin/tool"; chmod 755 "$d/bin/tool"
    for i in $(seq -w 1 250); do echo "file $i" > "$d/share/many/f$i"; done
    echo "version $label" > "$d/share/version"
    echo "spaced" > "$d/share/with space/a file"
    echo "fixed" > "$d/share/ro/data"
    chmod 555 "$d/share/ro"
    ln -s ../bin/tool "$d/share/tool-link"
    if [ "$label" != A ]; then echo "only in $label" > "$d/share/only-$label"; fi
}
seal() { # seal DIR VERSION [extra seal options...]
    local d=$1 v=$2; shift 2
    tb seal "$d" --name demo --version "$v" --source "demo 0123456789abcdef0123456789abcdef01234567" \
        --after qml-cache-purge --built 2026-11-09T00:00:00Z "$@"
}
fresh_slot() { chmod -R u+w "$SLOT" "$STATE" 2>/dev/null; rm -rf "$SLOT" "$STATE"; }
no_leftovers() { ! ls -d "$SLOT"/staging.* >/dev/null 2>&1 && [ ! -e "$STATE/switch" ]; }

# ---- The installed binary ----------------------------------------------

PROD=$HERE/../../bin/tam-bundle
if [ -x "$PROD" ]; then
    if ldd "$PROD" 2>/dev/null | grep -q libtam; then fail "tam-bundle loads a shared libtam"
    else pass "tam-bundle links libtam statically"; fi
    # The test build names $TAM_BUNDLE_FAULT; the installed build must not.
    if grep -q TAM_BUNDLE_FAULT "$PROD"; then fail "the installed build reacts to TAM_BUNDLE_FAULT"
    else pass "the installed build has no fault points"; fi
    if grep -q TAM_BUNDLE_PKEXEC "$PROD"; then fail "the installed build takes a stand-in for pkexec"
    else pass "the installed build runs only /usr/bin/pkexec"; fi
fi

# ---- Bundles --------------------------------------------------------------

for L in A B C; do make_tree "$BASE/$L" "$L"; done
seal "$BASE/A" 1.0; seal "$BASE/B" 2.0; seal "$BASE/C" 3.0
A=$(id_of "$BASE/A"); B=$(id_of "$BASE/B"); C=$(id_of "$BASE/C")

expect 0 "verify a sealed bundle" tb verify "$BASE/A"
mkdir -p "$BASE/A2"; make_tree "$BASE/A2" A
tb seal "$BASE/A2" --name demo --version 1.0 --source "demo 0123456789abcdef0123456789abcdef01234567" \
    --after qml-cache-purge --built 2030-01-01T00:00:00Z
[ "$(id_of "$BASE/A2")" = "$A" ] && pass "the same tree seals to the same hash (built time excluded)" \
    || fail "same tree, different hash: $(id_of "$BASE/A2") vs $A"
expect 1 "seal refuses a tree that is already sealed" tb seal "$BASE/A" --name demo --version 1.0

cp -a "$BASE/A" "$BASE/tampered"; chmod u+w "$BASE/tampered/share/ro"
echo "changed" > "$BASE/tampered/share/ro/data"
expect 1 "verify finds a changed file" tb verify "$BASE/tampered"
sed -i 's/^version 1.0$/version 1.1/' "$BASE/tampered/BUNDLE"
expect 1 "verify refuses a BUNDLE that no longer matches its hash" tb verify "$BASE/tampered"

cp -a "$BASE/A" "$BASE/newer"; sed -i '1s/format 1/format 3/' "$BASE/newer/BUNDLE"
expect 1 "a newer record format is refused" tb verify "$BASE/newer"
grep -q "format 3; this tam-bundle" "$BASE/out" && pass "the refusal names the format and the reader" \
    || fail "format refusal message: $(cat "$BASE/out")"

mkdir -p "$BASE/escape/a"; ln -s ../../outside "$BASE/escape/a/link"
expect 1 "seal refuses a link that leaves the tree" tb seal "$BASE/escape" --name x --version 1
mkdir -p "$BASE/nl"; touch "$BASE/nl/$(printf 'new\nline')"
expect 1 "seal refuses a path with a newline" tb seal "$BASE/nl" --name x --version 1
mkdir -p "$BASE/badstep"; echo x > "$BASE/badstep/f"
expect 1 "seal refuses an unknown post-install step" tb seal "$BASE/badstep" --name x --version 1 --after rm-rf

# ---- Switch, back, status, where -------------------------------------------

fresh_slot
expect 0 "dry-run plans the first switch before the slot exists" tb --dry-run switch "$BASE/A"
[ ! -e "$SLOT" ] && [ ! -e "$STATE" ] && pass "the first plan creates neither slot nor state" || fail "first plan wrote files"
mkdir -p "$SLOT/kept"
expect 0 "dry-run plans the first switch before state exists" tb --dry-run switch "$BASE/A"
[ ! -e "$STATE" ] && pass "planning an empty slot creates no state" || fail "empty-slot plan wrote state"
expect 0 "first switch into an empty slot" tb switch "$BASE/A"
[ "$(stat -c %a "$STATE/lock")" = 644 ] && pass "the state lock is readable by users planning a root-owned slot" || fail "private state lock"
chmod 444 "$STATE/lock"
expect 0 "dry-run can read a lock it cannot write" tb --dry-run switch "$BASE/B"
expect 0 "dry-run recovery can read a lock it cannot write" tb --dry-run recover
chmod 644 "$STATE/lock"
[ "$(live_id)" = "$A" ] && pass "A is live" || fail "live is $(live_id), wanted $A"
expect 0 "the live tree verifies in full" tb verify "$SLOT/current"
expect 0 "switching to the live bundle is a no-op" tb switch "$BASE/A"
expect 0 "switch A -> B" tb switch "$BASE/B"
[ "$(live_id)" = "$B" ] && [ -d "$SLOT/kept/$A" ] && pass "B is live and A is kept" || fail "after A -> B"
[ ! -e "$SLOT/current/share/only-A" ] || fail "a file removed in B is still live"
grep -q "^keep $A previous$" "$STATE/keep" && pass "A is recorded as previous" || fail "keep list: $(cat "$STATE/keep")"
expect 0 "where names the bundle of a live file" tb where "$SLOT/current/bin/tool"
expect 1 "where refuses a path outside the live tree" tb where /etc/hostname
expect 0 "check passes on an untouched live tree" tb check

if [ "$FS" = btrfs ]; then
    # In a slot of its own, so the main slot's history is unchanged.
    tb2() { "$BIN" --slot "$BASE/slot2" --state "$BASE/state2" "$@"; }
    tb2 --quiet switch "$BASE/B"
    tb2 switch "$BASE/C" > "$BASE/out" 2>&1
    grep -q "unchanged ones cloned" "$BASE/out" && pass "unchanged files are cloned on Btrfs" \
        || fail "no clones on Btrfs: $(cat "$BASE/out")"
fi

expect 0 "back returns to the previous bundle" tb back
[ "$(live_id)" = "$A" ] && [ -d "$SLOT/kept/$B" ] && pass "A is live again and B is kept" || fail "after back"
expect 0 "back again returns to B" tb back
[ "$(live_id)" = "$B" ] && pass "back twice is forward" || fail "after second back: $(live_id)"
expect 0 "switching to a kept bundle exchanges without staging" tb switch "$BASE/A"
[ "$(live_id)" = "$A" ] && no_leftovers && pass "A is live, nothing left over" || fail "kept switch"

# ---- Edits ---------------------------------------------------------------

chmod u+w "$SLOT/current/share/many/f001"
echo "hand edit" > "$SLOT/current/share/many/f001"
touch "$SLOT/current/share/extra-file"
expect 1 "check finds an edited and an extra file" tb check
expect 1 "switch stops on edits" tb switch "$BASE/C"
[ "$(live_id)" = "$A" ] && no_leftovers && pass "the stopped switch changed nothing" || fail "after refused switch"
expect 0 "switch --save-edits saves them and continues" tb --save-edits switch "$BASE/C"
[ "$(cat "$STATE/saved/$A/share/many/f001")" = "hand edit" ] && [ -e "$STATE/saved/$A/share/extra-file" ] \
    && pass "edited and extra files were saved" || fail "saved files"
[ "$(cat "$SLOT/current/share/many/f001")" = "file 001" ] && pass "C's own file is live" || fail "f001 after save"

# ---- Refusals ------------------------------------------------------------

mkdir -p "$BASE/H"; make_tree "$BASE/H" H; seal "$BASE/H" 9.0 --host not-this-host-$$
expect 1 "a bundle for another host is refused" tb switch "$BASE/H"
mkdir -p "$BASE/N"; make_tree "$BASE/N" N
seal "$BASE/N" 9.1 --needs "command tam-bundle-no-such-command-$$ arch=nothing void=nothing"
expect 1 "missing native needs are reported" tb needs "$BASE/N"
expect 1 "a bundle with missing needs is refused" tb switch "$BASE/N"
cp -a "$BASE/B" "$BASE/bad-file"; echo corrupt > "$BASE/bad-file/share/version"
chmod -R u+w "$SLOT/kept/$B" 2>/dev/null; rm -rf "$SLOT/kept/$B"
before=$(live_id)
expect 1 "a bundle file that does not match its hash stops the switch" tb switch "$BASE/bad-file"
[ "$(live_id)" = "$before" ] && no_leftovers && pass "the failed switch was undone" || fail "after corrupt bundle"
# A bundle whose directory was swapped for a link after sealing must not be
# able to read files outside itself.
mkdir -p "$BASE/swap/share" "$BASE/outside"; echo secret > "$BASE/swap/share/x"; echo secret > "$BASE/outside/x"
tb seal "$BASE/swap" --name swap --version 1
rm -rf "$BASE/swap/share"; ln -s ../outside "$BASE/swap/share"
before=$(live_id)
expect 1 "a link swapped into a bundle cannot redirect a read outside it" tb switch "$BASE/swap"
[ "$(live_id)" = "$before" ] && no_leftovers && pass "the refused switch left the slot as it was" || fail "after link swap"
if [ "$(id -u)" -ne 0 ]; then
    chmod 555 "$SLOT"
    expect 1 "an unwritable slot is refused with advice" tb switch "$BASE/A"
    grep -q "pkexec or sudo" "$BASE/out" && pass "the refusal suggests pkexec or sudo" || fail "unwritable message"
    chmod 755 "$SLOT"
fi

# ---- Prune and keep --------------------------------------------------------

fresh_slot
tb switch "$BASE/A"; tb switch "$BASE/B"; tb switch "$BASE/C"
expect 0 "keep protects a kept tree" tb keep "$A" "recovery anchor"
tb unkeep "$A"
expect 0 "prune without --yes only lists" tb prune
[ -d "$SLOT/kept/$A" ] && pass "nothing removed without --yes" || fail "prune removed without --yes"
expect 0 "prune --yes removes unprotected kept trees" tb prune --yes
[ ! -d "$SLOT/kept/$A" ] && [ -d "$SLOT/kept/$B" ] && pass "A pruned, previous B kept" || fail "prune result"

# ---- Older bundles -------------------------------------------------------

fresh_slot
for d in A B C; do tb switch "$BASE/$d"; done
ok=1
for id in "$A" "$B" "$C" "$A"; do
    tb back "$id" >/dev/null 2>&1 || ok=0
    [ "$(live_id)" = "$id" ] || ok=0
    tb verify "$SLOT/current" >/dev/null 2>&1 || ok=0
    tb --full check >/dev/null 2>&1 || ok=0
done
[ $ok = 1 ] && pass "rolled back to each of three bundles and verified each in full" || fail "older-bundle test"

# ---- Interruption (crash) test -------------------------------------------

check_recovered() { # check_recovered WHAT ALLOWED_IDS...
    local what=$1; shift
    local live; live=$(live_id)
    local okid=0
    for id in "$@"; do [ "$live" = "$id" ] && okid=1; done
    if [ $okid = 1 ] && tb verify "$SLOT/current" >/dev/null 2>&1 && \
       tb recover >/dev/null 2>&1 && no_leftovers && [ "$(live_id)" = "$live" ] && \
       tb verify "$SLOT/current" >/dev/null 2>&1 && tb check >/dev/null 2>&1 && \
       tbv status >/dev/null 2>&1; then
        return 0
    fi
    fail "$what: live '$live' (allowed: $*)"; tbv status >&2; return 1
}

points="after-intent stage-100 stage-200 after-stage after-exchange after-keep after-record"
crash_ok=0; crash_n=0
for p in $points; do
    fresh_slot; tb switch "$BASE/A"
    TAM_BUNDLE_FAULT=$p tb switch "$BASE/B" >/dev/null 2>&1
    rc=$?
    crash_n=$((crash_n + 1))
    if [ $rc -ne 99 ]; then fail "switch fault $p did not fire (exit $rc)"; continue; fi
    check_recovered "switch stopped at $p" "$A" "$B" || continue
    [ -d "$SLOT/kept/$A" ] || [ "$(live_id)" = "$A" ] || { fail "switch $p: A was lost"; continue; }
    tb switch "$BASE/C" >/dev/null 2>&1 && [ "$(live_id)" = "$C" ] || { fail "switch $p: next switch"; continue; }
    crash_ok=$((crash_ok + 1))
done
for p in after-intent after-exchange after-keep after-record; do
    fresh_slot; tb switch "$BASE/A"; tb switch "$BASE/B"
    TAM_BUNDLE_FAULT=$p tb back >/dev/null 2>&1
    rc=$?
    crash_n=$((crash_n + 1))
    if [ $rc -ne 99 ]; then fail "back fault $p did not fire (exit $rc)"; continue; fi
    check_recovered "back stopped at $p" "$A" "$B" || continue
    other=$A; [ "$(live_id)" = "$A" ] && other=$B
    [ -d "$SLOT/kept/$other" ] || { fail "back $p: $other was not kept"; continue; }
    crash_ok=$((crash_ok + 1))
done
[ $crash_ok = $crash_n ] && pass "recovered from all $crash_n fault points to exactly one whole bundle" \
    || fail "fault points: $crash_ok of $crash_n recovered"

# ---- Recover first ---------------------------------------------------------

# After an interrupted back, the tree that was live sits in kept/ under the
# target's name. prune must recover first, or it deletes that tree.
fresh_slot; tb switch "$BASE/A"; tb switch "$BASE/B"; tb switch "$BASE/C"
TAM_BUNDLE_FAULT=after-exchange tb back "$A" >/dev/null 2>&1
expect 0 "prune after an interrupted back recovers first" tb prune --yes
if [ "$(live_id)" = "$A" ] && [ -d "$SLOT/kept/$C" ] && no_leftovers && tb back >/dev/null 2>&1 && \
   [ "$(live_id)" = "$C" ]; then pass "the tree that was live survived prune, and back returns to it"
else fail "prune after an interrupted back lost the tree that was live"; tbv status >&2; fi

# A dry run changes nothing, even with a switch to recover.
fresh_slot; tb switch "$BASE/A"
TAM_BUNDLE_FAULT=after-exchange tb switch "$BASE/B" >/dev/null 2>&1
expect 4 "a dry-run prune stops at an interrupted switch" tb --dry-run prune
expect 4 "a dry-run switch stops at an interrupted switch" tb --dry-run switch "$BASE/C"
expect 4 "a dry-run back stops at an interrupted switch" tb --dry-run back
expect 4 "a dry-run keep stops at an interrupted switch" tb --dry-run keep "$A" anchor
[ -e "$STATE/switch" ] && [ "$(live_id)" = "$B" ] && pass "dry runs left the interrupted switch as it was" \
    || fail "a dry run changed the slot"

# Reading commands answer from the live tree and warn.
for c in check "where $SLOT/current/bin/tool" after; do
    # shellcheck disable=SC2086
    "$BIN" --slot "$SLOT" --state "$STATE" $c >"$BASE/out" 2>&1; rc=$?
    if [ $rc = 0 ] && grep -q "was interrupted" "$BASE/out"; then pass "${c%% *} answers and warns of an interrupted switch"
    else fail "${c%% *} during an interrupted switch (exit $rc)"; sed 's/^/        /' "$BASE/out" >&2; fi
done

expect 0 "keep recovers an interrupted switch first" tb keep "$A" anchor
[ -d "$SLOT/kept/$A" ] && no_leftovers && grep -qE "^keep[[:space:]]+$A[[:space:]]+anchor" "$STATE/keep" 2>/dev/null \
    && pass "keep protected the tree recovery moved to kept/" || { fail "keep after recovery"; cat "$STATE/keep" >&2; }

fresh_slot; tb switch "$BASE/A"
TAM_BUNDLE_FAULT=after-exchange tb switch "$BASE/B" >/dev/null 2>&1
expect 0 "unkeep recovers an interrupted switch first" tb unkeep "$A"
no_leftovers && [ -d "$SLOT/kept/$A" ] && pass "unkeep left a clean slot" || fail "unkeep after recovery"

# Kill at random moments: staging is slowed so kills land inside it as well.
fresh_slot; tb switch "$BASE/A"
rounds=${TAM_BUNDLE_KILL_ROUNDS:-30}; killed=0; kill_ok=0
targets=("$BASE/B" "$BASE/C" "$BASE/A")
for i in $(seq 1 "$rounds"); do
    tb prune --yes >/dev/null 2>&1
    t=${targets[$((i % 3))]}
    TAM_BUNDLE_SLOW_US=150 "$BIN" --quiet --slot "$SLOT" --state "$STATE" switch "$t" >/dev/null 2>&1 &
    pid=$!
    sleep "0.0$((RANDOM % 9))"
    kill -9 $pid 2>/dev/null && killed=$((killed + 1))
    wait $pid 2>/dev/null
    check_recovered "kill round $i" "$A" "$B" "$C" && kill_ok=$((kill_ok + 1))
done
[ $kill_ok = "$rounds" ] && pass "recovered after $rounds random kills ($killed landed mid-run)" \
    || fail "random kills: $kill_ok of $rounds recovered"

# ---- Privilege ------------------------------------------------------------

# A stand-in for pkexec: it records its arguments, then runs the command as
# this user from /, with a cleared environment and PKEXEC_UID set, as pkexec
# runs it as root from /root. FAKE_EXIT fails the way pkexec does;
# FAKE_RM removes a file after the run.
FAKE=$BASE/fake-pkexec
cat > "$FAKE" <<'EOF_FAKE'
#!/bin/sh
printf '%s\n' "$@" > "$FAKE_ARGS"
[ -n "${FAKE_EXIT:-}" ] && exit "$FAKE_EXIT"
uid=$(id -u)
rm_after=${FAKE_RM:-}
cd / && env -i PATH=/usr/bin:/bin PKEXEC_UID="$uid" "$@"
code=$?
[ -n "$rm_after" ] && rm -f "$rm_after"
exit $code
EOF_FAKE
chmod 755 "$FAKE"
ARGS=$BASE/pk-args
pk() { FAKE_ARGS=$ARGS TAM_BUNDLE_PKEXEC=$FAKE "$BIN" --slot "$SLOT" --state "$STATE" "$@"; }
me=$(id -u)
deferred() { grep -c " after qml-cache-purge deferred to uid $me\$" "$1/log"; }
# Results of handed-over steps go to session.<uid>; never into the log.
user_ran() { grep -c " after qml-cache-purge skipped\$" "$1/session.$me" 2>/dev/null; }
log_ran() { grep -c " after qml-cache-purge skipped\$" "$1/log"; }

fresh_slot; rm -rf "$BASE/state-rel"
(cd "$BASE" && FAKE_ARGS=$ARGS TAM_BUNDLE_PKEXEC=$FAKE "$BIN" --slot "$SLOT" --state state-rel switch A) \
    >"$BASE/out" 2>&1
code=$?
if [ $code = 0 ] && [ "$(sed -n 's/^live: *//p' < <("$BIN" --slot "$SLOT" --state "$BASE/state-rel" status))" = "$A" ]
then pass "switch through pkexec makes the bundle live"
else fail "switch through pkexec (exit $code)"; sed 's/^/        /' "$BASE/out" >&2; fi
grep -qx "$BASE/A" "$ARGS" && grep -qx "$BASE/state-rel" "$ARGS" \
    && pass "relative bundle and state paths reach pkexec as absolute paths" \
    || fail "pkexec arguments: $(tr '\n' ' ' < "$ARGS")"
head -1 "$ARGS" | grep -q '^/' && pass "pkexec runs this program by its absolute path" \
    || fail "pkexec program: $(head -1 "$ARGS")"
[ "$(deferred "$BASE/state-rel")" = 1 ] && [ "$(user_ran "$BASE/state-rel")" = 1 ] \
    && [ "$(grep -c 'qml-cache-purge: skipped in the test build' "$BASE/out")" = 1 ] \
    && pass "the root run defers the session step and the user's run takes it, once" \
    || fail "session step hand-over: $(grep after "$BASE/state-rel/log" | tr '\n' '|')"
[ "$(log_ran "$BASE/state-rel")" = 0 ] && pass "the user's result stays out of the root run's log" \
    || fail "the user's result went into the log"
[ "$(stat -c '%u %a' "$BASE/state-rel/session.$me")" = "$me 644" ] \
    && pass "the root run gives the user their own results file, 0644" \
    || fail "session file: $(stat -c '%u %a' "$BASE/state-rel/session.$me" 2>&1)"
"$BIN" --slot "$SLOT" --state "$BASE/state-rel" status | grep -q "^session:   uid $me: .* after qml-cache-purge skipped\$" \
    && pass "status shows the user's last result" \
    || fail "status: $("$BIN" --slot "$SLOT" --state "$BASE/state-rel" status | tr '\n' '|')"

fresh_slot
expect 0 "switch through pkexec, again" pk switch "$BASE/A"
expect 0 "switch to a second bundle through pkexec" pk switch "$BASE/B"
expect 0 "back through pkexec" pk back
[ "$(live_id)" = "$A" ] && pass "back through pkexec restores the first bundle" || fail "live after back: $(live_id)"
[ "$(deferred "$STATE")" = 3 ] && [ "$(user_ran "$STATE")" = 3 ] \
    && pass "each switch and back hands its session step to the user" \
    || fail "hand-overs: $(deferred "$STATE") deferred, $(user_ran "$STATE") run"
expect 0 "keep through pkexec" pk keep "$B" anchor
grep -q "$B (anchor)" < <(tbv status) && pass "keep through pkexec protects the tree" || fail "keep through pkexec"
[ "$(user_ran "$STATE")" = 3 ] && pass "keep runs no session step" || fail "keep ran a session step"

rm -f "$ARGS"
expect 0 "a dry run needs no pkexec" pk --dry-run switch "$BASE/C"
expect 0 "status needs no pkexec" pk status
[ ! -e "$ARGS" ] && pass "pkexec was not asked for a dry run or status" || fail "pkexec ran: $(tr '\n' ' ' < "$ARGS")"

FAKE_EXIT=126 expect 1 "a refused authorization fails" pk switch "$BASE/C"
grep -q "not authorized" "$BASE/out" && pass "the refusal says it was not authorized" || fail "126 message: $(cat "$BASE/out")"
FAKE_EXIT=127 expect 1 "pkexec unable to run fails" pk switch "$BASE/C"
grep -q "sudo" "$BASE/out" && grep -q "tam-bundle after" "$BASE/out" \
    && pass "with no polkit agent, it says to run as root and then \`tam-bundle after\`" \
    || fail "127 message: $(cat "$BASE/out")"
expect 1 "a missing pkexec fails" env TAM_BUNDLE_PKEXEC="$BASE/no-such-pkexec" "$BIN" --slot "$SLOT" --state "$STATE" switch "$BASE/C"
grep -q "not installed" "$BASE/out" && pass "the refusal names the missing pkexec" || fail "missing pkexec: $(cat "$BASE/out")"
[ "$(live_id)" = "$A" ] && no_leftovers && pass "failed authorizations leave the slot untouched" \
    || fail "after failed authorizations: live $(live_id)"

# Run as root by pkexec without this program waiting (`pkexec tam-bundle`
# typed by hand): the session step is deferred, and `after` runs it.
before=$(log_ran "$STATE")
expect 0 "switch with PKEXEC_UID set defers the session step" env PKEXEC_UID="$me" "$BIN" --slot "$SLOT" --state "$STATE" switch "$BASE/C"
! grep -q "skipped in the test build" "$BASE/out" && [ "$(deferred "$STATE")" = 4 ] \
    && pass "the deferred step is logged and not run" || fail "PKEXEC_UID run: $(cat "$BASE/out")"
expect 0 "after runs the live bundle's steps as the user" tbv after
[ "$(log_ran "$STATE")" = $((before + 1)) ] && pass "after ran the deferred step, recorded in the user's own log" \
    || fail "after: $(log_ran "$STATE") runs"

# The user never creates the results file: without one, the result is not
# recorded, and the user is told.
FAKE_RM=$STATE/session.$me expect 0 "switch through pkexec with the results file removed" pk switch "$BASE/B"
grep -q "could not record" "$BASE/out" && [ ! -e "$STATE/session.$me" ] \
    && pass "with no results file the user is told, and none is created" || fail "no results file: $(cat "$BASE/out")"

# The state is readable, so status, check and where need no privilege.
modes=$(stat -c %a "$STATE" "$STATE/stat" "$STATE/log" "$STATE/stat/$B" | tr '\n' ' ')
[ "$modes" = "755 755 644 644 " ] && pass "state directories and files are readable by everyone" \
    || fail "state modes: $modes"

# ---- Lock ------------------------------------------------------------------

if command -v flock >/dev/null; then
    flock "$STATE/lock" sleep 7 &
    lpid=$!
    sleep 0.3
    expect 1 "a second tam-bundle waits, then refuses while the slot is locked" tb back
    grep -q "another tam-bundle" "$BASE/out" && pass "the refusal says another tam-bundle is working" \
        || fail "lock message: $(cat "$BASE/out")"
    kill $lpid 2>/dev/null; wait $lpid 2>/dev/null
fi

if [ $FAILS -eq 0 ]; then echo "All tam-bundle tests passed ($FS)."; exit 0; fi
echo "$FAILS tam-bundle test(s) failed ($FS)." >&2
exit 1
