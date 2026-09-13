#!/usr/bin/env bash
# Frame generation against the engine: the sixteen-byte frames themselves.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$root/tools/analysis/frametest.py" \
     "$root/build/oracle" "$root/build/frametest" \
     "${DLL:-$root/dll/1995/B32_TTS.DLL}" "${1:-$root/tests/corpus.txt}"
