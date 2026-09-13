#!/usr/bin/env bash
# Diphone expansion to acoustic targets, against the engine's own fetches.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/segtest.py" \
     "$root/build/oracle" "${DLL:-$root/dll/1995/B32_TTS.DLL}" \
     "${1:-$root/tests/corpus.txt}"
