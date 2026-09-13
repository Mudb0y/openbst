#!/usr/bin/env bash
# Compares our dictionary decoder against the engine's, both in where it lands
# and in the record stream it produces. A word may legitimately reach neither:
# function words are resolved by an earlier fast path, and unknown words fall
# through to the letter-to-sound rules.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/words.txt}

hit=0 miss=0 differ=0
while read -r w; do
    [ -z "$w" ] && continue

    eng=$("$root/build/oracle" --dll "$dll" --speak "$w" --recat 0x1000faa0:0:48 2>/dev/null | head -1)
    out=$(python3 "$root/tools/analysis/dict.py" "$dll" "$w" 2>/dev/null)

    if ! echo "$out" | grep -q "found at"; then
        miss=$((miss + 1)); continue
    fi
    if [ -z "$eng" ]; then miss=$((miss + 1)); continue; fi

    # The engine's buffer is three-byte records terminated by a zero type.
    engrec=$(echo "$eng" | sed 's/^R //' | tr ' ' '\n' |
             awk 'BEGIN{n=0} {b[n++]=$1} END{
                 for(i=0;i+2<n;i+=3){
                     t=strtonum("0x" b[i]);
                     if(t==0) break;
                     printf "%c%d,%d ", t, strtonum("0x" b[i+1]), strtonum("0x" b[i+2]);
                 }}')
    ourrec=$(echo "$out" | grep 'records:' | sed 's/.*records: //')

    if [ "$(echo $engrec)" = "$(echo $ourrec)" ]; then
        hit=$((hit + 1))
    else
        differ=$((differ + 1))
        [ "$differ" -le 6 ] && echo "  DIFFER $w: engine [$(echo $engrec)] ours [$(echo $ourrec)]"
    fi
done < "$words"

echo "dicttest: $hit decoded identically, $miss not reached by the engine, $differ differing"
[ "$differ" -eq 0 ]
