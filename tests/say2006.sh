#!/usr/bin/env bash
# The 2006 English build end to end: our text-to-frames and our lattice
# against the audio the original produces. Say_TTS takes a wide string, so the
# oracle is driven with wstr rather than str.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/2006/dll_eng.dll}
map=${MAP:-2006ENG}
tables=${TABLES:-78e10,78dd0,79450,1ea2c,1eb2c,0,1,5,1}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

same=0 diff=0 shown=0
while IFS= read -r text; do
    [ -z "$text" ] && continue
    "$root/build/oracle" --dll "$dll" --limit 800000000 --call Init_TTS \
        --call "Say_TTS:wstr:$text" --raw --out "$work/ref.pcm" >/dev/null 2>&1
    "$root/build/saytest" --map "$map" --voice 0 \
        --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
        --tables "$tables" "$dll" "$text" > "$work/ours.pcm" 2>/dev/null
    n=$(stat -c%s "$work/ref.pcm")
    if [ "$n" -gt 0 ] && cmp -s "$work/ref.pcm" "$work/ours.pcm"; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        if [ "$shown" -lt 3 ]; then
            shown=$((shown + 1))
            echo "  DIFFER: $text ($n vs $(stat -c%s "$work/ours.pcm") bytes)"
        fi
    fi
done < "$corpus"

echo "say2006: $same utterances identical, $diff differing"
[ "$diff" -eq 0 ]
