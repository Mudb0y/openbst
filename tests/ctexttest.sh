#!/usr/bin/env bash
# The C front end against the engine: the normalised buffer word for word, and
# the dictionary record stream for every word that reaches the dictionary.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/normwords.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$root/build/normtest" "$dll" < "$words" > "$work/ours.txt"

nok=0 nbad=0 nskip=0 dok=0 dbad=0 lok=0 lbad=0
while read -r w mine _flags kind recs; do
    eng=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null | grep '^B' | head -1 |
          sed 's/.*| //' | tr ' ' '\n' |
          awk 'NF{v=strtonum("0x" $1); if(v==0) exit; printf "%c", v}')
    if [ -n "$eng" ]; then
        if [ "$eng" = "$mine" ]; then nok=$((nok + 1))
        else nbad=$((nbad + 1)); [ "$nbad" -le 6 ] && echo "  NORM $w: engine [$eng] C [$mine]"; fi
    else nskip=$((nskip + 1)); fi

    if [ "$kind" = "lts" ]; then
        engstream=$("$root/build/oracle" --dll "$dll" --speak "$w" --snap 0x1000efee:0x10019330:48 2>/dev/null |
                    head -1 | sed 's/^S | //' | cut -d' ' -f2-)
        [ -z "$engstream" ] && continue
        n=$(echo "$recs" | wc -w)
        engtrim=$(echo "$engstream" | cut -d' ' -f1-"$n")
        if [ "$engtrim" = "$(echo $recs)" ]; then lok=$((lok + 1))
        else lbad=$((lbad + 1)); [ "$lbad" -le 6 ] && echo "  LTS $w: engine [$engtrim] C [$(echo $recs)]"; fi
        continue
    fi
    [ "$kind" != "dict" ] && continue
    engrec=$("$root/build/oracle" --dll "$dll" --speak "$w" --recat 0x1000faa0:0:48 2>/dev/null |
             head -1 | sed 's/^R //' | tr ' ' '\n' |
             awk 'BEGIN{n=0}{b[n++]=$1} END{
                 for(i=0;i+2<n;i+=3){t=strtonum("0x" b[i]); if(t==0) break;
                     printf "%c%d,%d ", t, strtonum("0x" b[i+1]), strtonum("0x" b[i+2]);}}')
    [ -z "$engrec" ] && continue
    if [ "$(echo $engrec)" = "$(echo $recs)" ]; then dok=$((dok + 1))
    else dbad=$((dbad + 1)); [ "$dbad" -le 6 ] && echo "  DICT $w: engine [$(echo $engrec)] C [$(echo $recs)]"; fi
done < "$work/ours.txt"

echo "ctexttest: normaliser $nok identical / $nbad differing / $nskip skipped"
echo "           dictionary $dok identical / $dbad differing"
echo "           rules      $lok identical / $lbad differing"
[ "$nbad" -eq 0 ] && [ "$dbad" -eq 0 ] && [ "$lbad" -eq 0 ]
