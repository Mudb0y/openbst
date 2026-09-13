#!/usr/bin/env bash
# Accent assignment against the engine: the codes written into the stream.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/acctest.py" \
     "$root/build/oracle" "$root/build/acctest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
