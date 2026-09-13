#!/usr/bin/env bash
# The intonation contour against the engine's pitch records.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/contest.py" \
     "$root/build/oracle" "$root/build/contest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
