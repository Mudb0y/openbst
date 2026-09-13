#!/usr/bin/env bash
# Our pair scan and diphone expansion against the targets the engine fetched.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/targettest.py" \
     "$root/build/oracle" "$root/build/pairtest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
