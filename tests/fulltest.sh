#!/usr/bin/env bash
# Everything after the tokeniser, in our own code, against the engine's audio.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/fulltest.py" \
     "$root/build/oracle" "$root/build/fulltest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
