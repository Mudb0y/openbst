#!/usr/bin/env bash
# Verifies the phonological rule pass's cursors, with no rules applied.
#
# Run one sentence at a time: the engine samples once per position of its own
# stream, so rows from different sentences cannot be concatenated and aligned.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

tot_ok=0 tot_bad=0 tot_fb=0 lines=0
while IFS= read -r t; do
    [ -z "$t" ] && continue
    "$root/build/oracle" --dll "$dll" --phrules "$t" 2>/dev/null | grep '^B' > "$work/s.txt"
    "$root/build/oracle" --dll "$dll" --cursors "$t" 2>/dev/null > "$work/c.txt"
    [ -s "$work/s.txt" ] || continue
    [ -s "$work/c.txt" ] || continue
    lines=$((lines + 1))
    out=$("$root/build/curtest" "$dll" "$work/s.txt" "$work/c.txt" 2>/dev/null | tail -1)
    ok=$(echo "$out" | sed 's/.*curtest: \([0-9]*\) .*/\1/')
    bad=$(echo "$out" | sed 's/.*match, \([0-9]*\) differ.*/\1/')
    fb=$(echo "$out" | sed 's/.*(\([0-9]*\) previous.*/\1/')
    tot_ok=$((tot_ok + ok)); tot_bad=$((tot_bad + bad)); tot_fb=$((tot_fb + fb))
done < "$corpus"

echo "curtest: $tot_ok cursor states match, $tot_bad differ, over $lines sentences"
echo "         ($tot_fb previous-cursor values shifted by the pass's own rewrites)"
[ "$tot_bad" -eq 0 ]
