#!/usr/bin/env bash
# The 2006 English build end to end: our text-to-frames and our lattice
# against the audio the original produces. Say_TTS takes a wide string, so the
# oracle is driven with wstr rather than str.
#
# The engine's start-up writes twenty silent samples at index zero of the
# output buffer, over the first twenty of what it has already produced, so the
# comparison ignores those.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/2006/dll_eng.dll}
map=${MAP:-2006ENG}
tables=${TABLES:-78e10,78dd0,79450,1ea2c,1eb2c,0,1,5,1}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cmp.py" <<'PYEOF'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
ok = (len(a) > 0 and len(b) == len(a)
      and a[:40] == b'\0' * 40 and a[40:] == b[40:])
sys.exit(0 if ok else 1)
PYEOF

same=0 diff=0 shown=0
while IFS= read -r text; do
    [ -z "$text" ] && continue
    "$root/build/oracle" --dll "$dll" --limit 800000000 --call Init_TTS \
        --call "Say_TTS:wstr:$text" --raw --out "$work/ref.pcm" >/dev/null 2>&1
    "$root/build/saytest" --map "$map" --voice 0 \
        --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
        --tables "$tables" "$dll" "$text" > "$work/ours.pcm" 2>/dev/null
    if python3 "$work/cmp.py" "$work/ref.pcm" "$work/ours.pcm"; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        if [ "$shown" -lt 3 ]; then
            shown=$((shown + 1))
            echo "  DIFFER: $text ($(stat -c%s "$work/ref.pcm") vs $(stat -c%s "$work/ours.pcm") bytes)"
        fi
    fi
done < "$corpus"

echo "say2006: $same utterances identical, $diff differing"
[ "$diff" -eq 0 ]
