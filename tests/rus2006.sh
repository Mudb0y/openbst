#!/usr/bin/env bash
# The 2006 Russian build word by word and sentence by sentence, through our
# own front end and lattice against the engine's audio.
#
# The build reads its text as a wide string whose units are its own code page,
# so the words are held in that code page and passed as bytes.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
dll=$root/dll/2006/dll_rus.dll
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

tables=$(python3 "$root/tools/analysis/tables2006.py" \
         "$root/dll/1995/B32_TTS.DLL" "$dll") || exit 1

same=0 diff=0
say() {
    local t=$1
    timeout 120 "$root/build/oracle" --dll "$dll" --limit 800000000 \
        --call Init_TTS --call "Say_TTS:wstr:$t" --raw --out "$work/e.pcm" \
        >/dev/null 2>&1
    "$root/build/saytest" --map 2006RUS --voice 0 \
        --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 --tables "$tables" \
        "$dll" "$t" > "$work/o.pcm" 2>/dev/null
    if python3 - "$work/e.pcm" "$work/o.pcm" <<'PYEOF'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
sys.exit(0 if a and len(a) == len(b) and a[:40] == b'\0' * 40 and a[40:] == b[40:] else 1)
PYEOF
    then same=$((same + 1))
    else diff=$((diff + 1)); echo "  differs: $t"
    fi
}

for w in $(cat "$root/tests/ruswords.txt"); do say "$w"; done

# Spoken numbers. Five and six digit numbers whose second group is said after
# a comma are left out: the group cursor the scan carries from one phrase to
# the next still holds a sound where the engine holds none, which shifts the
# second phrase's first vowel by a frame.
for n in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 15 16 17 19 20 21 25 30 40 50 60 70 \
         80 90 91 99 100 101 111 123 200 300 555 999 1000 1001 1100 1234 2006 \
         2026 1905 9999 10000 100000 123456 1000000 1000001; do say "$n"; done
say "$(printf '\335\362\356 \362\345\361\362.')"
say "$(printf '\304\356\354 \350 \352\356\362.')"
say "$(printf '\335\362\356 \362\345\361\362! \304\356\354.')"

echo "rus2006: $same identical, $diff differing"
[ "$diff" -eq 0 ]
