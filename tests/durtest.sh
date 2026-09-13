#!/usr/bin/env bash
# Captures (rate byte, record byte, duration) at the instruction where the
# engine computes a segment's duration, and requires our model to agree.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

: > "$work/t.txt"
while IFS= read -r text; do
    [ -z "$text" ] && continue
    paste \
      <("$root/build/oracle" --dll "$dll" --speak "$text" --regmem 0x1000452d:edi:0:2 2>/dev/null | awk '/^M/{print $4}') \
      <("$root/build/oracle" --dll "$dll" --speak "$text" --regmem 0x1000452d:esi:0:1 2>/dev/null | awk '/^M/{print $3}') \
      <("$root/build/oracle" --dll "$dll" --speak "$text" --hook 0x10006bc0:2   2>/dev/null | awk '/^call/{print $3}') \
      >> "$work/t.txt"
done < "$corpus"

echo "durtest: $(wc -l < "$work/t.txt") samples"
"$root/build/durtest" "$dll" "$work/t.txt"
