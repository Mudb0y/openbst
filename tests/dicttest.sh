#!/usr/bin/env bash
# For each word, compares where our dictionary decoder lands against where the
# engine's own lookup lands. A word may legitimately reach neither: function
# words are resolved by an earlier fast path and unknown words fall through to
# the letter-to-sound rules.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
words=${1:-$root/tests/words.txt}

hit=0 miss=0 differ=0
while read -r w; do
    [ -z "$w" ] && continue
    eng=$("$root/build/oracle" --dll "$dll" --speak "$w" --hook 0x10012500:3 2>/dev/null |
          awk '/^call/{printf "%x:%d\n", and($3,0xffffffff), and($2,0xffff); exit}')
    out=$(python3 "$root/tools/analysis/dict.py" "$dll" "$w" 2>/dev/null)
    if echo "$out" | grep -q "found at"; then
        a=$(echo "$out" | grep -o 'found at 0x[0-9a-f]*' | grep -o '[0-9a-f]*$')
        p=$(echo "$out" | grep -o 'nibble [0-9]*$' | grep -o '[0-9]*')
        mine="$a:$p"
    else
        mine=""
    fi

    if [ -n "$eng" ] && [ "$eng" = "$mine" ]; then hit=$((hit + 1))
    elif [ -z "$eng" ] && [ -z "$mine" ]; then miss=$((miss + 1))
    elif [ -z "$eng" ]; then miss=$((miss + 1))   # engine took an earlier path
    else differ=$((differ + 1)); echo "  DIFFER $w: engine '$eng' ours '$mine'"; fi
done < "$words"

echo "dicttest: $hit located identically, $miss not reached by the engine, $differ differing"
[ "$differ" -eq 0 ]
