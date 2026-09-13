#!/usr/bin/env bash
# Compares which letter-to-sound rules fire, and in what order, against the
# engine's own choices. The engine's normalised buffer is fed in directly so
# this exercises the rule engine rather than the normaliser upstream of it.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/ltswords.txt}

same=0 differ=0 skip=0
while read -r w; do
    [ -z "$w" ] && continue
    cap=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null)
    eng=$(echo "$cap" | awk '/^call/{next} 1' >/dev/null; \
          "$root/build/oracle" --dll "$dll" --speak "$w" --hook 0x1000dbe0:1 2>/dev/null |
          awk '/^call/{printf "%x ", and($2,0xffffffff)}')
    [ -z "$eng" ] && { skip=$((skip + 1)); continue; }

    buf=$(echo "$cap" | grep '^B' | head -1 | sed 's/.*| //' |
          tr ' ' '\n' | awk 'NF{printf "%c", strtonum("0x" $1)}' | sed 's/ *$//')
    [ -z "$buf" ] && { skip=$((skip + 1)); continue; }

    ours=$(python3 "$root/tools/analysis/lts.py" "$dll" --buf "$buf" 2>/dev/null |
           sed 's/^[^ ]* *//' | tr ' ' '\n' | sed 's/.*@0x//' | tr '\n' ' ')
    if [ "$(echo $eng)" = "$(echo $ours)" ]; then same=$((same + 1))
    else differ=$((differ + 1))
         [ "$differ" -le 6 ] && echo "  DIFFER $w (buffer $buf): engine [$(echo $eng)] ours [$(echo $ours)]"
    fi
done < "$words"
echo "ltstest: $same identical rule sequences, $differ differing, $skip skipped"
[ "$differ" -eq 0 ]
