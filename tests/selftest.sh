#!/usr/bin/env bash
# The library against the answers the original binaries gave, with nothing
# but the library present. This is the one test a clone can run: every other
# one compares against an original under the emulator.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
"$root/build/selftest" "$root/tests/golden.txt"
