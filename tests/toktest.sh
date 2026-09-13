#!/usr/bin/env bash
# The tokeniser against the engine: the token sequence a text produces.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/toktest.py" \
     "$root/build/oracle" "$root/build/toktest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
