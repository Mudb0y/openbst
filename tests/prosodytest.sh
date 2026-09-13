#!/usr/bin/env bash
# Captures the engine's gain and pitch smoothing steps over a corpus and
# requires our implementations to reproduce each one exactly.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for mode in gain pitch; do
    : > "$work/$mode.txt"
    while IFS= read -r text; do
        [ -z "$text" ] && continue
        "$root/build/oracle" --dll "$dll" --"$mode" "$text" 2>/dev/null >> "$work/$mode.txt"
    done < "$corpus"
    "$root/build/prosodytest" "$dll" "$work/$mode.txt" "$mode"
done
