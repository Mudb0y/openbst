#!/usr/bin/env bash
# The 1998 English module through our own front end.
#
# Our frames against the ones the engine hands its host. The comparison is of
# the sequence of distinct coefficient sets rather than of frames one by one,
# because this build holds an unvoiced frame for eight short periods where the
# 1995 one holds it for a single long one. That changes how many frames a
# sound takes without changing the sound, so a frame-by-frame comparison
# desynchronises on the first unvoiced segment and reports nothing useful
# after it.
#
# What is compared is therefore what the dictionary, the rules, the diphone
# inventory and the interpolator decide between them. The voice settings this
# build defaults differently are not yet matched.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
lang=${DLL:-$root/dll/1998/KGMENG.DLL}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# pulse, noise and gain live in the driver; the log pair and the durations in
# the language module.
tables=c870,c830,ceb0,7ad20,7af20,7b120
params=0x50,0xA0,3,0,0,0,0x30,0x10,-0x12

cat > "$work/cmp.py" <<'PY'
import sys


def runs(path):
    d = open(path, "rb").read()
    out = []
    for i in range(len(d) // 16):
        f = d[i * 16:i * 16 + 16]
        v = f[2:3] + f[4:14]
        if not out or out[-1] != v:
            out.append(v)
    return out


a, b = runs(sys.argv[1]), runs(sys.argv[2])
n = min(len(a), len(b))
ok = sum(1 for i in range(n) if a[i] == b[i])
print(ok, max(len(a), len(b)) - ok, len(a), len(b))
PY

ok=0 bad=0 words=0
for w in dog cat house water number people; do
    "$root/build/neoracle" --eof -1 --limit 400000000 --lang "$lang" \
        --engine "$w.
" --frames "$work/theirs" >/dev/null 2>&1
    "$root/build/saytest" --frames --ne --params "$params" --tables "$tables" \
        "$lang" "$w." > "$work/ours" 2>/dev/null
    [ -s "$work/theirs" ] || continue
    words=$((words + 1))
    set -- $(python3 "$work/cmp.py" "$work/ours" "$work/theirs")
    ok=$((ok + $1)); bad=$((bad + $2))
    [ "$2" -ne 0 ] && echo "  $w: $1 of $(( $1 + $2 )) agree ($3 ours, $4 theirs)"
done

echo "ne98test: $words words, $ok coefficient sets identical, $bad differing"
[ "$words" -gt 0 ] && [ "$bad" -eq 0 ]
