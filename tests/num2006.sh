#!/usr/bin/env bash
# Spoken numbers through the thirteen 2006 language builds, against the
# engine's own audio sample for sample. Arabic and Greek read their own code
# pages, so they take bytes rather than a wide string; both of those builds
# say nothing at all for a digit, which is what the engine does.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cmp.py" <<'PYEOF'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
if not a:
    print("  no audio from the engine")
    sys.exit(1)
ok = len(b) == len(a) and a[40:] == b[40:]
sys.exit(0 if ok else 1)
PYEOF

NUMBERS="5 12 17 21 28 30 31 99 100 101 111 123 200 250 300 909 1000 1100 1234
         2006 12345 21000 100000 123456 500500 654321 999999 1000000 2000000"

same=0 diff=0
for l in ara dut eng fre ger gre heb ita jpn pol por rus spa; do
    d="$root/dll/2006/dll_$l.dll"
    tables=$(python3 "$root/tools/analysis/tables2006.py" \
             "$root/dll/1995/B32_TTS.DLL" "$d") || continue
    case $l in ara|gre) form=str;; *) form=wstr;; esac
    for t in $NUMBERS; do
        timeout 120 "$root/build/oracle" --dll "$d" --limit 800000000 \
            --call Init_TTS --call "Say_TTS:$form:$t" --raw --out "$work/r.pcm" \
            >/dev/null 2>&1
        "$root/build/saytest" --map "2006$(echo "$l" | tr a-z A-Z)" --voice 0 \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 --tables "$tables" \
            "$d" "$t" > "$work/o.pcm" 2>/dev/null
        if python3 "$work/cmp.py" "$work/r.pcm" "$work/o.pcm"; then
            same=$((same + 1))
        else
            diff=$((diff + 1))
            echo "  $l $t differs"
        fi
    done
done

echo "num2006: $same identical, $diff differing"
