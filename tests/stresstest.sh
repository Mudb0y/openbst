#!/usr/bin/env bash
# Word stress against the engine: which syllable takes the accent.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/stresstest.py" \
     "$root/build/oracle" "$root/build/stresstest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
