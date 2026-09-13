#!/usr/bin/env bash
# Compares the letter-to-sound path end to end: which rules fire, and the
# phoneme record buffer they build. The engine's normalised word buffer is fed
# in directly, so this exercises the rules and the record builder rather than
# the normaliser upstream of them.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/ltswords.txt}

same=0 differ=0 skip=0
while read -r w; do
    [ -z "$w" ] && continue

    cap=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null)
    buf=$(echo "$cap" | grep '^B' | head -1 | sed 's/.*| //' |
          tr ' ' '\n' | awk 'NF{printf "%c", strtonum("0x" $1)}' | sed 's/ *$//')
    [ -z "$buf" ] && { skip=$((skip + 1)); continue; }

    eng=$("$root/build/oracle" --dll "$dll" --speak "$w" --snap 0x1000efee:0x10019330:40 2>/dev/null |
          head -1 | sed 's/^S | //')
    [ -z "$eng" ] && { skip=$((skip + 1)); continue; }

    ours=$(python3 "$root/tools/analysis/lts.py" "$dll" --buf "$buf" 2>/dev/null |
           sed 's/.*buffer //')
    # Snapshot taken the moment the rule driver returns, before the suffix
    # and post-processing stages run. Compare the records only: the leading
    # count is not final at this point.
    n=$(echo "$ours" | wc -w)
    engtrim=$(echo "$eng" | cut -d' ' -f2-"$n")
    ourtrim=$(echo "$ours" | cut -d' ' -f2-"$n")

    if [ "$engtrim" = "$ourtrim" ]; then same=$((same + 1))
    else differ=$((differ + 1))
         [ "$differ" -le 5 ] && echo "  DIFFER $w: engine [$engtrim] ours [$ourtrim]"
    fi
done < "$words"
echo "ltstest: $same identical buffers, $differ differing, $skip skipped"
[ "$differ" -eq 0 ]
