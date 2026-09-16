#!/usr/bin/env bash
# Writes tests/golden.txt: for every build and every utterance, how many
# samples came out and a hash of them.
#
# The answers come from the original binaries, not from the tables compiled
# in, so tests/selftest.sh checks the library against the originals rather
# than against itself. That is why this needs the binaries and the test it
# writes for does not.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/tests/golden.txt
st=$root/build/selftest
w=$root/tests/words2006

: > "$out"
"$st" --write 1995 "$root/dll/1995/B32_TTS.DLL" "$w/eng.txt" >> "$out"
for l in ENG:eng DUT:dut FRN:fre GRM:ger ITL:ita SPN:spa; do
    "$st" --write "1998${l%%:*}" "$root/dll/1998/KGM${l%%:*}.DLL" \
          "$w/${l##*:}.txt" >> "$out"
done
for l in ara dut eng fre ger gre heb ita jpn pol por rus spa; do
    u=$(echo "$l" | tr a-z A-Z)
    list="$w/$l.txt"
    [ "$l" = rus ] && list="$root/tests/ruswords.txt"
    "$st" --write "2006$u" "$root/dll/2006/dll_$l.dll" "$list" >> "$out"
done

echo "golden: $(wc -l < "$out") utterances"
