#!/usr/bin/env bash
# The tilde marks, against the original engine.
#
# A tilde is a lead-in rather than a symbol: the engine reads it and the
# character after it as one mark and says neither by name. Speak now, tilde
# bar, closes the phrase where it stands, opens the next one as a
# continuation and gives the phrase it closed the fifth sentence type. The
# card's own manual describes it as beginning speech after the next word,
# which is what the hardware does with the phrases once it has them; inside
# the engine it is a phrase break at the mark.
#
# 1995 is checked on audio, sample for sample. The 1998 modules are checked
# on frames, the way ne98test does, because their oracle intercepts above the
# synthesizer. Two builds are left out and neither is about the mark: 1998
# French diverges from the engine on any two-word sentence, mark or no mark,
# which predates this and is not covered by any other test, and the 2006
# builds break the phrase in the right place but time the first one
# differently, which is not yet measured.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cmp.py" <<'PY'
import sys


def frames(path):
    d = open(path, "rb").read()
    return [d[i * 16:i * 16 + 16] for i in range(len(d) // 16)]


a, b = frames(sys.argv[1]), frames(sys.argv[2])
n = min(len(a), len(b))
k = 0
while k < n and a[k] == b[k]:
    k += 1
# Ours runs a few frames past the engine's capture in every case, baseline
# included, so what is asserted is that every frame the engine produced is
# ours as well.
sys.exit(0 if len(a) > 0 and k == len(a) else 1)
PY

pass=0 fail=0

# ---- 1995, on audio ------------------------------------------------------
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
# Where the mark sits relative to the words, how many marks there are, and
# whether anything precedes the first one. The last two must still be spoken
# by name: a lone tilde and a lone bar are not marks.
for text in \
    "one two." \
    "one ~| two." \
    "one ~|two." \
    "~|one two." \
    "one two ~| three." \
    "a ~| b ~| c." \
    "one ~| two ~| three ~| four." \
    "Hello there, how are you?" \
    "one | two." \
    "one ~ two."
do
    "$root/build/oracle" --dll "$dll" --speak "$text" --raw \
        --out "$work/ref.pcm" >/dev/null 2>&1
    printf '%s' "$text" | "$root/build/bstspeak" --build 1995 --raw \
        > "$work/mine.pcm" 2>/dev/null
    if [ -s "$work/ref.pcm" ] && cmp -s "$work/ref.pcm" "$work/mine.pcm"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL 1995: $text"
    fi
done

# ---- 1998, on frames -----------------------------------------------------
# Each module is driven in its own language: the engine and the library agree
# on a foreign word only by accident.
run98() {
    local lang=$1 log=$2 alog=$3 w1=$4 w2=$5 w3=$6
    local dll="$root/dll/1998/KGM$lang.DLL"
    local text
    for text in "$w1 $w2." "$w1 ~| $w2." "$w1 ~| $w2 ~| $w3." "~|$w1 $w2."; do
        "$root/build/neoracle" --eof -1 --limit 400000000 --lang "$dll" \
            --engine "$text
" --frames "$work/theirs" >/dev/null 2>&1
        "$root/build/saytest" --frames --ne --map "$lang" \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
            --tables "c870,c830,ceb0,$log,$alog,0" "$dll" "$text" \
            > "$work/ours" 2>/dev/null
        if python3 "$work/cmp.py" "$work/theirs" "$work/ours"; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "FAIL 1998$lang: $text"
        fi
    done
}

run98 ENG 7ad20 7af20 dog cat house
run98 DUT 26866 26a66 hond kat huis
run98 GRM 3304e 3324e hund katze haus
run98 ITL 210ce 212ce cane gatto casa
run98 SPN 137ec 139ec perro gato casa

echo "cmdtest: $pass identical, $fail differing"
[ "$fail" -eq 0 ]
