#!/usr/bin/env bash
# Sentence assembly against the engine: tokens to a phrase's phoneme stream.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/asmtest.py" \
     "$root/build/oracle" "$root/build/asmtest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
