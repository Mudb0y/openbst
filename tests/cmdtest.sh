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
# Only the 1995 build is checked. The 1998 and 2006 builds break the phrase
# in the same place but time it differently, and their per-build values are
# not yet measured, so asserting on them here would encode a guess.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
dll=${DLL:-$root/dll/1995/B32_TTS.DLL}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Where the mark sits relative to the words, how many marks there are, and
# whether anything precedes the first one.
texts=(
    "one two."
    "one ~| two."
    "one ~|two."
    "~|one two."
    "one two ~| three."
    "a ~| b ~| c."
    "one ~| two ~| three ~| four."
    "Hello there, how are you?"
    "one | two."
    "one ~ two."
)

echo "cmdtest: ${#texts[@]} texts against $(basename "$dll")"

pass=0 fail=0
for text in "${texts[@]}"; do
    "$root/build/oracle" --dll "$dll" --speak "$text" --raw \
        --out "$work/ref.pcm" >/dev/null 2>&1
    printf '%s' "$text" | "$root/build/bstspeak" --build 1995 --raw \
        > "$work/mine.pcm" 2>/dev/null

    if [ -s "$work/ref.pcm" ] && cmp -s "$work/ref.pcm" "$work/mine.pcm"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        a=$(stat -c%s "$work/ref.pcm" 2>/dev/null || echo 0)
        b=$(stat -c%s "$work/mine.pcm" 2>/dev/null || echo 0)
        echo "FAIL ($a vs $b bytes): $text"
    fi
done

echo "cmdtest: $pass identical, $fail differing"
[ "$fail" -eq 0 ]
