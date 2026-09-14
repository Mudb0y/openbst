#!/usr/bin/env bash
# The 2006 language builds through our own lattice: their frames rendered by
# our synthesizer against the audio their engine produced.
#
# Say_TTS takes a wide string in these builds. Handing it a narrow one leaves
# the converted buffer holding one character, so the engine says nothing and
# the comparison is silence against silence.
#
# The engine's own start-up writes a twenty-sample silent frame at index zero
# of the output buffer, which lands over the first twenty samples of what it
# has already produced, and the last twenty never reach the buffer. So its
# audio is ours with those twenty samples zeroed and twenty missing from the
# end, and that is what is checked.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/dll/1995/B32_TTS.DLL
text=${1:-"This is a test of the speech engine."}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

same=0 diff=0
for dll in "$root"/dll/2006/*.dll; do
    name=$(basename "$dll" .dll)
    tables=$(python3 "$root/tools/analysis/tables2006.py" "$ref" "$dll") || continue
    at=$(python3 "$root/tools/analysis/findfunc.py" "$root/dll/2006/dll_eng.dll" \
             0x10007180 0x40 "$dll" | awk '{print $2}')
    case "$at" in 0x*) ;; *) echo "  $name: renderer not found"; diff=$((diff+1)); continue;; esac
    "$root/build/oracle" --dll "$dll" --limit 800000000 --call Init_TTS \
        --call "Say_TTS:wstr:$text" --recat "$at:0:16" \
        --raw --out "$work/ref.pcm" 2>/dev/null |
        sed -n 's/^R /F /p' > "$work/f.txt"
    [ -s "$work/f.txt" ] || { echo "  $name: no frames captured"; diff=$((diff+1)); continue; }
    "$root/build/synth" --dll "$dll" --frames "$work/f.txt" --tables "$tables" \
        --out "$work/ours.wav" >/dev/null 2>&1
    tail -c +45 "$work/ours.wav" > "$work/ours.pcm"
    if python3 - "$work/ref.pcm" "$work/ours.pcm" <<'PY'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
ok = (len(a) > 4000 and len(b) == len(a) + 40
      and a[:40] == b'\0' * 40 and a[40:] == b[40:len(a)])
sys.exit(0 if ok else 1)
PY
    then
        same=$((same + 1))
    else
        diff=$((diff + 1))
        echo "  $name: $(stat -c%s "$work/ref.pcm") vs $(stat -c%s "$work/ours.pcm") bytes, differ"
    fi
done

echo "difftest2006: $same builds identical, $diff differing"
[ "$diff" -eq 0 ]
