#!/usr/bin/env bash
# The bar any phonological rule implementation has to clear.
#
# The rule pass rewrites relatively few positions, so an implementation that
# does nothing already scores well. Measuring against that baseline rather than
# against zero is the only way to tell whether a rule set is helping: a first
# attempt scored 5270 of 5568 positions, which looked like 94.6% until the
# identity turned out to score 5294.
set -u

root=$(cd "$(dirname "$0")/../.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
corpus=${1:-$root/tests/corpus.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

while IFS= read -r t; do
    [ -z "$t" ] && continue
    "$root/build/oracle" --dll "$dll" --phrules "$t" 2>/dev/null
done < "$corpus" > "$work/ph.txt"

python3 - "$work/ph.txt" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
tot = same = unchanged = streams = 0
changed = 0
b = None
for l in lines:
    if l.startswith('B'):
        b = [int(x, 16) for x in l.split('|')[1].split()]
    elif l.startswith('A') and b is not None:
        a = [int(x, 16) for x in l.split('|')[1].split()]
        if len(a) == len(b):
            streams += 1
            tot += len(a)
            s = sum(1 for x, y in zip(a, b) if x == y)
            same += s
            changed += len(a) - s
            if s == len(a):
                unchanged += 1
        b = None
print("identity baseline: %d of %d positions (%.1f%%), %d of %d streams unchanged"
      % (same, tot, 100.0 * same / max(tot, 1), unchanged, streams))
print("the rule pass rewrites %d positions in total" % changed)
PY
