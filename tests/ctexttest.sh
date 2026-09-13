#!/usr/bin/env bash
# The C normaliser against the engine's own normalised buffer, word for word.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/normwords.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$root/build/normtest" "$dll" < "$words" > "$work/ours.txt"

ok=0 bad=0 skip=0
while read -r w mine _flags; do
    eng=$("$root/build/oracle" --dll "$dll" --lts "$w" 2>/dev/null | grep '^B' | head -1 |
          sed 's/.*| //' | tr ' ' '\n' |
          awk 'NF{v=strtonum("0x" $1); if(v==0) exit; printf "%c", v}')
    if [ -z "$eng" ]; then skip=$((skip + 1)); continue; fi
    if [ "$eng" = "$mine" ]; then ok=$((ok + 1))
    else bad=$((bad + 1)); [ "$bad" -le 8 ] && echo "  DIFFER $w: engine [$eng] C [$mine]"; fi
done < "$work/ours.txt"

echo "ctexttest: $ok identical, $bad differing, $skip skipped"
[ "$bad" -eq 0 ]
