#!/usr/bin/env bash
# The tables compiled into the library against the tables in the original
# binaries, over every build and more text than the lift itself saw.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
same=0 diff=0
run() {
    local out
    out=$("$root/build/lifttest" "$@" 2>&1) || true
    echo "$out" | grep -v identical >&2 || true
    set -- $(echo "$out" | tail -1)
    same=$((same + $2))
    diff=$((diff + $4))
}
run 1995 "$root/dll/1995/B32_TTS.DLL" "$root/tests/words2006/eng.txt"
for l in ENG:eng DUT:dut FRN:fre GRM:ger ITL:ita SPN:spa; do
    run "1998${l%%:*}" "$root/dll/1998/KGM${l%%:*}.DLL" "$root/tests/words2006/${l##*:}.txt"
done
for l in ara dut eng fre ger gre heb ita jpn pol por rus spa; do
    u=$(echo "$l" | tr a-z A-Z)
    w="$root/tests/words2006/$l.txt"
    [ "$l" = rus ] && w="$root/tests/ruswords.txt"
    run "2006$u" "$root/dll/2006/dll_$l.dll" "$w"
done
echo "lifttest: $same identical, $diff differing"
[ "$diff" -eq 0 ]
