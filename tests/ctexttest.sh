#!/usr/bin/env bash
# The C word front end against the engine: the normalised buffer word for word,
# and the phoneme code stream, whichever path produced it.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/normwords.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$root/build/normtest" "$dll" < "$words" > "$work/ours.txt"

nok=0 nbad=0 nskip=0 dok=0 dbad=0 lok=0 lbad=0
while read -r w mine flags kind stream; do
    eng=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null | grep '^B' | head -1 |
          sed 's/.*| //' | tr ' ' '\n' |
          awk 'NF{v=strtonum("0x" $1); if(v==0) exit; printf "%c", v}')
    if [ -n "$eng" ]; then
        if [ "$eng" = "$mine" ]; then nok=$((nok + 1))
        else nbad=$((nbad + 1)); [ "$nbad" -le 6 ] && echo "  NORM $w: engine [$eng] C [$mine]"; fi
    else nskip=$((nskip + 1)); fi

    [ -z "$stream" ] && continue
    engstream=$("$root/build/oracle" --dll "$dll" --speak "$w" --snap 0x1000effd:0x10019330:64 \
                2>/dev/null | head -1 | sed 's/^S | //' | cut -d' ' -f2-)
    [ -z "$engstream" ] && continue
    n=$(echo "$stream" | wc -w)
    engtrim=$(echo "$engstream" | cut -d' ' -f1-"$n")
    if [ "$engtrim" = "$(echo $stream)" ]; then
        if [ "$kind" = dict ]; then dok=$((dok + 1)); else lok=$((lok + 1)); fi
    elif [ "$kind" = dict ]; then
        dbad=$((dbad + 1))
        [ "$dbad" -le 6 ] && echo "  DICT $w: engine [$engtrim] C [$(echo $stream)]"
    else
        lbad=$((lbad + 1))
        [ "$lbad" -le 6 ] && echo "  LTS $w: engine [$engtrim] C [$(echo $stream)]"
    fi
done < "$work/ours.txt"

echo "ctexttest: normaliser $nok identical / $nbad differing / $nskip skipped"
echo "           dictionary $dok identical / $dbad differing"
echo "           rules      $lok identical / $lbad differing"
[ "$nbad" -eq 0 ] && [ "$dbad" -eq 0 ] && [ "$lbad" -eq 0 ]
