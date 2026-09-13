#!/usr/bin/env bash
# Captures the engine's own interpolation steps over a corpus and requires our
# implementation to reproduce each one exactly.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

: > "$work/all.txt"
while IFS= read -r text; do
    [ -z "$text" ] && continue
    "$root/build/oracle" --dll "$dll" --interp "$text" 2>/dev/null >> "$work/all.txt"
done < "$corpus"

echo "interptest: $(grep -c '^B' "$work/all.txt") steps captured from $(basename "$corpus")"
"$root/build/interptest" "$dll" "$work/all.txt"
