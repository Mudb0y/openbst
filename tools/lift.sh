#!/usr/bin/env bash
# Writes src/data from the original binaries: one file of tables to a build,
# and the index the library looks a build up in.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/src/data
lift=$root/build/lift

rm -f "$out"/*.c
mkdir -p "$out"

names=()
run() {
    local name=$1 dll=$2
    shift 2
    "$lift" "$name" "$dll" "$out" "$@" 2>&1 | tail -1
    names+=("$name")
}

run 1995 "$root/dll/1995/B32_TTS.DLL" "$root/tests/words2006/eng.txt"
for l in ENG DUT FRN GRM ITL SPN; do
    case $l in
        ENG) w=eng;; DUT) w=dut;; FRN) w=fre;;
        GRM) w=ger;; ITL) w=ita;; SPN) w=spa;;
    esac
    run "1998$l" "$root/dll/1998/KGM$l.DLL" "$root/tests/words2006/$w.txt"
done
for l in ara dut eng fre ger gre heb ita jpn pol por rus spa; do
    u=$(echo "$l" | tr a-z A-Z)
    lists=("$root/tests/words2006/$l.txt")
    [ "$l" = rus ] && lists=("$root/tests/ruswords.txt")
    run "2006$u" "$root/dll/2006/dll_$l.dll" "${lists[@]}"
done

{
    echo "/* Written by tools/lift.sh. The index the library looks a build up"
    echo "   in; the tables themselves are one file to a build beside this. */"
    echo
    echo '#include <string.h>'
    echo '#include "bst_text.h"'
    echo
    for n in "${names[@]}"; do echo "extern const bst_lifted BST_DATA_$n;"; done
    echo
    echo "static const struct { const char *name; const bst_lifted *d; } INDEX[] = {"
    for n in "${names[@]}"; do echo "    { \"$n\", &BST_DATA_$n },"; done
    echo "};"
    echo
    echo "const bst_lifted *bst_lifted_for(const char *build) {"
    echo "    if (!build) return NULL;"
    echo "    for (size_t i = 0; i < sizeof INDEX / sizeof INDEX[0]; i++)"
    echo "        if (strcmp(INDEX[i].name, build) == 0) return INDEX[i].d;"
    echo "    return NULL;"
    echo "}"
} > "$out/lifted.c"

echo "lift: ${#names[@]} builds, $(du -sh "$out" | cut -f1) of C"
