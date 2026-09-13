#!/usr/bin/env bash
# Compares our suffix stripper against the engine's normalised word buffer.
# Words resolved before the rule driver runs are skipped, not counted.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/normwords.txt}

ok=0 bad=0 skip=0
while read -r w; do
    [ -z "$w" ] && continue
    eng=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null | grep '^B' | head -1 |
          sed 's/.*| //' | tr ' ' '\n' |
          awk 'NF{v=strtonum("0x" $1); if(v==0) exit; printf "%c", v}')
    [ -z "$eng" ] && { skip=$((skip + 1)); continue; }
    ours=$(python3 "$root/tools/analysis/normalise.py" "$dll" "$w" 2>/dev/null | awk '{print $2}')
    if [ "$eng" = "$ours" ]; then ok=$((ok + 1))
    else bad=$((bad + 1)); [ "$bad" -le 8 ] && echo "  DIFFER $w: engine [$eng] ours [$ours]"; fi
done < "$words"
echo "normtest: $ok identical, $bad differing, $skip skipped"
[ "$bad" -eq 0 ]
