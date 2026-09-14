#!/usr/bin/env bash
# The 1998 family through our own front end.
#
# Each module's frames against the ones its engine hands its host. The
# comparison is of the sequence of distinct coefficient sets rather than of
# frames one by one, because this generation holds an unvoiced frame for eight
# short periods where the 1995 one holds it for a single long one: that changes
# how many frames a sound takes without changing the sound, so a positional
# comparison desynchronises on the first unvoiced segment.
#
# English is required to agree exactly, and does: every coefficient set, and
# every frame byte for byte until the silence after the word, which this build
# spends on repeated single-period frames where the 1995 one spends it on
# eight-period ones. Both are silent, the gain being zero either way.
#
# The other five have a table map written by tools/analysis/mkmap.py, which
# reproduces the English one address for address but has not been checked
# against its own engine. Their agreement is reported, not required.
#
# The mark shift is handled: each language numbers its marks from the end of
# its own inventory, by 0, +9, -8, +8, -12 and -13 for English, Dutch, French,
# German, Italian and Spanish, and the library keeps its streams in English
# numbering and shifts only where it indexes one of the engine's tables.
#
# German is the furthest along and says what is left. Its stream is two bytes
# from the engine's -- the sentence type the assembler writes, 4 against 3, and
# the last sound of the word, 0x04 against 0x08 -- and its frames run identical
# for thirteen before ours snaps to a target the engine glides towards. So what
# remains is the tables the transition durations and the contour are built
# from, and those are the ones no method here has been able to place with
# confidence: translating them through the matched code gives single-vote
# answers that make every language worse when applied.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

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


def lead(x, y):
    a, b = open(x, "rb").read(), open(y, "rb").read()
    n = min(len(a), len(b)) // 16
    k = 0
    while k < n and a[k * 16:k * 16 + 16] == b[k * 16:k * 16 + 16]:
        k += 1
    return k


a, b = runs(sys.argv[1]), runs(sys.argv[2])
n = min(len(a), len(b))
ok = sum(1 for i in range(n) if a[i] == b[i])
print(ok, max(len(a), len(b)), lead(sys.argv[1], sys.argv[2]))
PY

# The log pair's file offset differs per module; pulse, noise and gain live in
# the driver and are the same for all of them.
run_lang() {
    local lang=$1 log=$2 alog=$3
    shift 3
    local dll="$root/dll/1998/KGM$lang.DLL"
    local ok=0 tot=0 lead=0
    for w in "$@"; do
        "$root/build/neoracle" --eof -1 --limit 400000000 --lang "$dll" \
            --engine "$w.
" --frames "$work/theirs" >/dev/null 2>&1
        "$root/build/saytest" --frames --ne --map "$lang" \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
            --tables "c870,c830,ceb0,$log,$alog,0" "$dll" "$w." \
            > "$work/ours" 2>/dev/null
        [ -s "$work/theirs" ] || continue
        set -- $(python3 "$work/cmp.py" "$work/ours" "$work/theirs") "$@"
        ok=$((ok + $1)); tot=$((tot + $2)); lead=$((lead + $3))
        shift 3
    done
    echo "$ok $tot $lead"
}

set -- $(run_lang ENG 7ad20 7af20 dog cat house water number people)
eng_ok=$1 eng_tot=$2 eng_lead=$3
echo "ne98test: English $eng_ok of $eng_tot coefficient sets, $eng_lead frames"
echo "          identical from the start"

for spec in "DUT 26866 26a66 hond kat huis" \
            "FRN 1c582 1c782 chien chat maison" \
            "GRM 3304e 3324e hund katze haus" \
            "ITL 210ce 212ce cane gatto casa" \
            "SPN 137ec 139ec perro gato casa"; do
    set -- $(run_lang $spec)
    echo "          $(echo $spec | cut -d' ' -f1) $1 of $2, $3 frames (map not yet checked)"
done

[ "$eng_ok" -gt 0 ] && [ "$eng_ok" -eq "$eng_tot" ]
