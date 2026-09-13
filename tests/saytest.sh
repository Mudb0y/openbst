#!/usr/bin/env bash
# The whole engine in our own code, against the original's audio: text in,
# samples out, nothing of the original used but its tables.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

same=0 diff=0 shown=0
while IFS= read -r text; do
    [ -z "$text" ] && continue
    "$root/build/oracle" --dll "$dll" --speak "$text" --raw --out "$work/ref.pcm" \
        >/dev/null 2>&1
    "$root/build/saytest" "$dll" "$text" > "$work/ours.pcm" 2>/dev/null
    if cmp -s "$work/ref.pcm" "$work/ours.pcm"; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        if [ "$shown" -lt 3 ]; then
            shown=$((shown + 1))
            echo "  DIFFER: $text ($(stat -c%s "$work/ref.pcm") vs $(stat -c%s "$work/ours.pcm") bytes)"
        fi
    fi
done < "$corpus"

echo "saytest: $same utterances identical, $diff differing"
[ "$diff" -eq 0 ]
