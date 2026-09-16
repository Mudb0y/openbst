#!/usr/bin/env bash
# Every build's lattice tables against the 1995 build's. Needs no binaries.
set -uf
root=$(cd "$(dirname "$0")/.." && pwd)
"$root/build/tabletest"
