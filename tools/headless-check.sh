#!/bin/bash
# Agentic load verification: boots a scratch headless Rack (its own user dir,
# so a running GUI session is untouched), loads a patch, and greps the scratch
# log for the truth -- plugin loaded, modules created, every cable landed.
# Exits 0 on PASS. Usage: tools/headless-check.sh <patch.vcv>
set -u
PATCH="${1:?usage: headless-check.sh <patch.vcv>}"
RACK="/Applications/VCV Rack 2 Pro.app/Contents/MacOS/Rack"
REAL="$HOME/Library/Application Support/Rack2/plugins-mac-arm64"
SCRATCH="${TMPDIR:-/tmp}/rack-headless-check"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/plugins-mac-arm64"
# Only what the patch needs; Core is built in.
for p in tnn1t1s-lives ImpromptuModular Fundamental SlimeChild-Substation; do
    [ -d "$REAL/$p" ] && ln -s "$REAL/$p" "$SCRATCH/plugins-mac-arm64/$p"
done

"$RACK" -h -u "$SCRATCH" "$PATCH" >/dev/null 2>&1 &
PID=$!
sleep 12
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

LOG="$SCRATCH/log.txt"
fail=0
say() { echo "  $1"; }

if grep -q "Could not load plugin.*tnn1t1s-lives" "$LOG"; then
    say "FAIL plugin rejected: $(grep 'Could not load plugin.*tnn1t1s-lives' "$LOG" | sed 's/.*: //')"
    fail=1
elif grep -q "Loaded plugin tnn1t1s-lives" "$LOG"; then
    say "ok   plugin loaded"
else
    say "FAIL plugin never loaded"; fail=1
fi

for m in "LIVES TAPES" "LIVES CLIPS"; do
    if grep -q "Creating module TNN1T1S $m" "$LOG"; then
        say "ok   module created: $m"
    else
        say "FAIL module missing: $m"; fail=1
    fi
done

MISSING=$(grep -c "Cannot find Cable\|Cannot load model" "$LOG")
if [ "$MISSING" -eq 0 ]; then
    say "ok   every cable landed"
else
    say "FAIL $MISSING cable/model failures:"; grep "Cannot find Cable\|Cannot load model" "$LOG" | head -5 | sed 's/^/       /'
    fail=1
fi

if grep -q "CLIPS loaded" "$LOG"; then
    say "ok   $(grep 'CLIPS loaded' "$LOG" | sed 's/.*loadClip] //')"
fi

[ $fail -eq 0 ] && echo "PASS headless-check: $PATCH" || echo "FAIL headless-check: $PATCH"
exit $fail
