#!/usr/bin/env bash
# The 1998 audio against the original lattice, sample for sample.
#
# ne98test proves the frames and stops there, because the 16-bit oracle
# intercepts above the synthesizer. This runs KNGMM's own lattice on the same
# frames and compares what comes out with what the library says.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

same=0 diff=0
# The log pair sits at its own offset in each language module; the excitation
# and gain tables the frames do not need.
for spec in ENG:7ad20:7af20 DUT:26866:26a66 FRN:1c582:1c782 \
            GRM:3304e:3324e ITL:210ce:212ce SPN:137ec:139ec; do
    l=${spec%%:*}
    rest=${spec#*:}
    log=${rest%%:*}
    alog=${rest#*:}
    dll=$root/dll/1998/KGM$l.DLL
    [ -f "$dll" ] || continue
    while IFS= read -r text; do
        [ -z "$text" ] && continue
        "$root/build/saytest" --frames --ne --map "$l" \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
            --tables "c870,c830,ceb0,$log,$alog,0" "$dll" "$text" > "$work/f.bin" 2>/dev/null
        [ -s "$work/f.bin" ] || continue
        "$root/build/kngoracle" "$root/dll/1998/KNGMM.DLL" "$work/f.bin" \
            "$work/theirs.pcm" >/dev/null 2>&1 || continue
        printf '%s' "$text" | "$root/build/bstspeak" --build "1998$l" --raw \
            > "$work/ours.pcm" 2>/dev/null
        if cmp -s "$work/theirs.pcm" "$work/ours.pcm"; then
            same=$((same + 1))
        else
            diff=$((diff + 1))
            echo "  1998$l differs: $text"
        fi
    done < "$root/tests/corpus.txt"
done

echo "kngtest: $same utterances identical, $diff differing"
[ "$diff" -eq 0 ] && [ "$same" -gt 0 ]
