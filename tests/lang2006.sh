#!/usr/bin/env bash
# The other 2006 language builds through our own front end and lattice, each
# with the directory carried over from the English build. Say_TTS takes a wide
# string, and the engine's start-up writes twenty silent samples at index zero
# of the output buffer, so the comparison allows for those.
#
# The ones listed here match outright; the report says how far each gets before
# it diverges, so a change can be seen to help or not.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cmp.py" <<'PYEOF'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
if not a:
    print("  no audio from the engine")
    sys.exit(1)
ok = len(b) == len(a) and a[:40] == b'\0' * 40 and a[40:] == b[40:]
i = 0
while i < min(len(a), len(b)) and a[i] == b[i]:
    i += 1
print("  %-4s engine %7d ours %7d, %7d bytes match"
      % ("same" if ok else "diff", len(a), len(b), i))
sys.exit(0 if ok else 1)
PYEOF

same=0 diff=0
run() {
    local l=$1; shift
    local t="$*"
    local d="$root/dll/2006/dll_$l.dll"
    local tables
    tables=$(python3 "$root/tools/analysis/tables2006.py" \
             "$root/dll/1995/B32_TTS.DLL" "$d") || return
    timeout 120 "$root/build/oracle" --dll "$d" --limit 800000000 \
        --call Init_TTS --call "Say_TTS:${form:-wstr}:$t" --raw --out "$work/r.pcm" \
        >/dev/null 2>&1
    "$root/build/saytest" --map "2006$(echo "$l" | tr a-z A-Z)" --voice 0 \
        --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 --tables "$tables" \
        "$d" "$t" > "$work/o.pcm" 2>/dev/null
    printf '  %s' "$l"
    if python3 "$work/cmp.py" "$work/r.pcm" "$work/o.pcm"; then
        same=$((same + 1))
    else
        diff=$((diff + 1))
    fi
}

# The Arabic build reads its own code page a byte at a time rather than as a
# wide string, so the text is given as bytes and the call form differs.
form=str run ara "$(printf '\312\307\310 \312\310\355\321.')"
form=wstr
run dut Dit is een test.
run fre Ceci est un test.
run ger Dies ist ein Test.
# Greek reads its own code page, so the text is given as bytes.
run gre "$(printf '\364\357 \363\360\337\364\351.')"
run heb ze mivchan.
run ita Questo e un test.
run jpn kore wa tesuto desu.
run pol To jest test.
run por Isto e um teste.
run spa Esto es una prueba.

# A digit is silent in these two, where the others read it.
form=str run ara "$(printf '\312\307\310 5.')"
form=wstr
run jpn kore wa 12 desu.

echo "lang2006: $same identical, $diff differing"
