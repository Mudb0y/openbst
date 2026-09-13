#!/usr/bin/env bash
# The phonological rule pass against the engine, and against the identity,
# which is the bar that matters: the pass rewrites few positions.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

while IFS= read -r text; do
    [ -z "$text" ] && continue
    "$root/build/oracle" --dll "$dll" --phrules "$text" 2>/dev/null
done < "$corpus" > "$work/ph.txt"

"$root/build/phtest" "$dll" "$work/ph.txt"
