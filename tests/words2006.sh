#!/usr/bin/env bash
# Ordinary words through the thirteen 2006 language builds, against the
# engine's audio sample for sample.
#
# Greek and Arabic read their own code pages, so their word lists are held in
# those encodings and passed as bytes; Arabic is read a byte at a time rather
# than as a wide string. Russian has a corpus of its own in rus2006.sh.
#
# French carries accents, and its list is held in the code page the build
# reads, the Windows Latin one, the same way the Greek list is.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cmp.py" <<'PYEOF'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
sys.exit(0 if a and len(a) == len(b) and a[40:] == b[40:] else 1)
PYEOF

same=0 diff=0
for l in ara dut eng fre ger gre heb ita jpn pol por spa; do
    d="$root/dll/2006/dll_$l.dll"
    list="$root/tests/words2006/$l.txt"
    [ -f "$list" ] || continue
    tables=$(python3 "$root/tools/analysis/tables2006.py" \
             "$root/dll/1995/B32_TTS.DLL" "$d") || continue
    case $l in ara) form=str;; *) form=wstr;; esac
    extra="$root/tests/words2006/$l-cp.txt"
    [ -f "$extra" ] && list="$list $extra"
    while IFS= read -r w2; do
        [ -z "$w2" ] && continue
        timeout 120 "$root/build/oracle" --dll "$d" --limit 800000000 \
            --call Init_TTS --call "Say_TTS:$form:$w2." --raw --out "$work/r.pcm" \
            >/dev/null 2>&1
        "$root/build/saytest" --map "2006$(echo "$l" | tr a-z A-Z)" --voice 0 \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 --tables "$tables" \
            "$d" "$w2." > "$work/o.pcm" 2>/dev/null
        if python3 "$work/cmp.py" "$work/r.pcm" "$work/o.pcm"; then
            same=$((same + 1))
        else
            diff=$((diff + 1))
            echo "  $l $w2 differs"
        fi
    done < <(cat $list)
done

echo "words2006: $same identical, $diff differing"
