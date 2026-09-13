#!/usr/bin/env bash
# The pair scan against the engine: every segment and transition it emits.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/pairtest.py" \
     "$root/build/oracle" "$root/build/pairtest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
