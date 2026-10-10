#!/bin/bash
# Load verification against the packaged plugin: boots a scratch headless Rack
# (its own user dir, so a running GUI session is untouched) with the
# .vcvplugin from `make dist` dropped into its plugins folder, loads a patch,
# and reads the scratch log: plugin extracted and loaded, every Phases module
# in the patch created, every cable landed, no warnings from the plugin.
# Exits 0 on PASS. Usage: tools/headless-check.sh <patch.vcv>
set -u
PATCH="${1:?usage: headless-check.sh <patch.vcv>}"
SLUG=tnn1t1s-phases
RACK="/Applications/VCV Rack 2 Pro.app/Contents/MacOS/Rack"
REAL="$HOME/Library/Application Support/Rack2/plugins-mac-arm64"
SCRATCH="${TMPDIR:-/tmp}/rack-headless-check"
# Seconds a headless Rack needs to start, extract the package and load a small
# patch on an M-series laptop. Observation from runs of this script, not a
# guarantee; raise it if the log stops before "Loaded patch".
BOOT_S=12

PKG=$(ls dist/$SLUG-*-mac-arm64.vcvplugin 2>/dev/null | head -1)
[ -n "$PKG" ] || { echo "FAIL no dist/$SLUG-*.vcvplugin; run make dist first"; exit 1; }

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/plugins-mac-arm64"
cp "$PKG" "$SCRATCH/plugins-mac-arm64/"
# Other plugins the patch uses, linked from the real install; Core is built in.
for p in $(zstd -dc "$PATCH" | tar -xOf - | grep -o '"plugin": "[^"]*"' | cut -d'"' -f4 | sort -u); do
    [ "$p" = Core ] || [ "$p" = "$SLUG" ] && continue
    [ -d "$REAL/$p" ] && ln -s "$REAL/$p" "$SCRATCH/plugins-mac-arm64/$p"
done

"$RACK" -h -u "$SCRATCH" "$PATCH" >/dev/null 2>&1 &
PID=$!
sleep "$BOOT_S"
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

LOG="$SCRATCH/log.txt"
fail=0
say() { echo "  $1"; }

if grep -q "Could not load plugin.*$SLUG" "$LOG"; then
    say "FAIL plugin rejected: $(grep "Could not load plugin.*$SLUG" "$LOG" | sed 's/.*: //')"
    fail=1
elif grep -q "Loaded plugin $SLUG" "$LOG"; then
    say "ok   plugin loaded from package"
else
    say "FAIL plugin never loaded"; fail=1
fi

WANT=$(zstd -dc "$PATCH" | tar -xOf - | grep -c "\"plugin\": \"$SLUG\"")
GOT=$(grep -c "Creating module TNN1T1S PHASES" "$LOG")
if [ "$GOT" -eq "$WANT" ]; then
    say "ok   $GOT of $WANT Phases modules created"
else
    say "FAIL $GOT of $WANT Phases modules created"; fail=1
fi

MISSING=$(grep -c "Cannot find Cable\|Cannot load model" "$LOG")
if [ "$MISSING" -eq 0 ]; then
    say "ok   every cable landed"
else
    say "FAIL $MISSING cable/model failures:"; grep "Cannot find Cable\|Cannot load model" "$LOG" | head -5 | sed 's/^/       /'
    fail=1
fi

WARNS=$(grep -i "warn\|error" "$LOG" | grep -i -c "$SLUG\|PHASES\|Expr\.cpp\|Phase\.cpp")
if [ "$WARNS" -eq 0 ]; then
    say "ok   no warnings from $SLUG"
else
    say "FAIL $WARNS warnings from $SLUG:"; grep -i "warn\|error" "$LOG" | grep -i "$SLUG\|PHASES\|Expr\.cpp\|Phase\.cpp" | head -5 | sed 's/^/       /'
    fail=1
fi

[ $fail -eq 0 ] && echo "PASS headless-check: $PATCH" || echo "FAIL headless-check: $PATCH"
exit $fail
