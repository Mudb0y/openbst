#!/usr/bin/env bash
# Ordinary words through the thirteen 2006 language builds, against the
# engine's audio sample for sample.
#
# Greek and Arabic read their own code pages, so their word lists are held in
# those encodings and passed as bytes; Arabic is read a byte at a time rather
# than as a wide string. Russian has a corpus of its own in rus2006.sh.
#
# A build whose language needs more than the alphabet gets a second list,
# <lang>-cp.txt, held in the code page that build reads, which is not always
# the one the name suggests. Say_TTS converts the caller's wide string with a
# code page it names itself, but what the tables are written in is a separate
# question, and only some builds carry a map between the two. French, Italian,
# Portuguese, German and Dutch have that map and take the Windows Latin page;
# Spanish has none and takes the DOS page its tables are written in; Polish
# takes the DOS Central European one and Hebrew the DOS Hebrew one. The lists
# are held in whichever that is, and the engine is given the same bytes.
#
# The 1998 modules share the plain lists, so nothing that only the 2006
# generation can read may go in those.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A text the build says nothing to is a result like any other, so an empty
# pair counts as agreement -- but only because the oracle is checked for
# having run: a failed run also leaves nothing behind, and the two must not
# be confused. The capture file is deleted first for the same reason.
cat > "$work/cmp.py" <<'PYEOF'
import os
import sys
a = open(sys.argv[1], 'rb').read() if os.path.exists(sys.argv[1]) else b''
b = open(sys.argv[2], 'rb').read() if os.path.exists(sys.argv[2]) else b''
sys.exit(0 if len(a) == len(b) and a[40:] == b[40:] else 1)
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
        rm -f "$work/r.pcm" "$work/o.pcm"
        if ! timeout 120 "$root/build/oracle" --dll "$d" --limit 800000000 \
            --call Init_TTS --call "Say_TTS:$form:$w2." --raw --out "$work/r.pcm" \
            >/dev/null 2>&1; then
            diff=$((diff + 1))
            echo "  $l $w2 oracle failed"
            continue
        fi
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
