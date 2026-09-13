#!/usr/bin/env bash
# Derives the engine's phoneme inventory by transcribing a corpus and counting
# the distinct symbols. Deriving it beats asserting it: rerun this against any
# build and the inventory it actually uses falls out.
set -u

root=$(cd "$(dirname "$0")/../.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus-generated.txt}
lines=${2:-80}

echo "inventory: $(basename "$dll"), first $lines lines of $(basename "$corpus")"

head -n "$lines" "$corpus" | while IFS= read -r t; do
    [ -z "$t" ] && continue
    "$root/build/oracle" --dll "$dll" --phonemes "$t" 2>/dev/null
done | tr ' ' '\n' | grep -v '^$' | sort | uniq -c | sort -rn |
while read -r count sym; do
    case "$sym" in
        '$'*|';'|"'"|'"'|'`'|'~I'*|'sl') kind=marker ;;
        *)                               kind=phoneme ;;
    esac
    printf '%-8s %-10s %s\n' "$sym" "$kind" "$count"
done
