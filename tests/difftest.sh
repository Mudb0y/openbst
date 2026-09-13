#!/usr/bin/env bash
# Renders each line of a corpus twice, once through the original engine under
# emulation and once through our synthesizer fed the engine's own frames, and
# requires the two to be identical sample for sample.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "difftest: $(wc -l < "$corpus") lines against $(basename "$dll")"

pass=0 fail=0 line=0
while IFS= read -r text; do
    [ -z "$text" ] && continue
    line=$((line + 1))

    "$root/build/oracle" --dll "$dll" --frames "$text" >"$work/f.txt" 2>/dev/null
    "$root/build/oracle" --dll "$dll" --speak "$text" --raw --out "$work/ref.pcm" >/dev/null 2>&1
    "$root/build/synth" --dll "$dll" --frames "$work/f.txt" >/dev/null 2>&1
    "$root/build/synth" --dll "$dll" --frames "$work/f.txt" --out "$work/mine.wav" >/dev/null 2>&1
    tail -c +45 "$work/mine.wav" >"$work/mine.pcm"

    if cmp -s "$work/ref.pcm" "$work/mine.pcm"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        a=$(stat -c%s "$work/ref.pcm" 2>/dev/null || echo 0)
        b=$(stat -c%s "$work/mine.pcm" 2>/dev/null || echo 0)
        echo "FAIL line $line ($a vs $b bytes): $text"
    fi
done < "$corpus"

echo "difftest: $pass identical, $fail differing"
[ "$fail" -eq 0 ]
