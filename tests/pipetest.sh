#!/usr/bin/env bash
# Everything after the word front end, in our own code, against the engine's
# audio: accents, the pair scan, the contour, frame generation, the lattice.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/pipetest.py" \
     "$root/build/oracle" "$root/build/pipetest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
