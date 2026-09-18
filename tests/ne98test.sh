#!/usr/bin/env bash
# The 1998 family through our own front end.
#
# Each module's frames against the ones its engine hands its host, byte for
# byte, for as long as the engine keeps producing them. This build spends the
# silence after a word on repeated single-period frames and stops sooner than
# we do, so the comparison runs to the end of the engine's own output and the
# frames we go on to produce after it are not counted against us.
#
# The mark shift is handled: each language numbers its marks from the end of
# its own inventory, by 0, +9, -8, +8, -12 and -13 for English, Dutch, French,
# German, Italian and Spanish, and the library keeps its streams in English
# numbering and shifts only where it indexes one of the engine's tables.
#
# All six are required to agree on every word.
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
print(k, len(b))
PY

# The log pair's file offset differs per module; pulse, noise and gain live in
# the driver and are the same for all of them.
run_lang() {
    local lang=$1 log=$2 alog=$3
    shift 3
    local dll="$root/dll/1998/KGM$lang.DLL"
    local ok=0 words=0 same=0 tot=0
    for w in "$@"; do
        "$root/build/neoracle" --eof -1 --limit 400000000 --lang "$dll" \
            --engine "$w.
" --frames "$work/theirs" >/dev/null 2>&1
        "$root/build/saytest" --frames --ne --map "$lang" \
            --params 0x50,0xA0,3,0,0,0,0x30,0x10,-0x12,0 \
            --tables "c870,c830,ceb0,$log,$alog,0" "$dll" "$w." \
            > "$work/ours" 2>/dev/null
        [ -s "$work/theirs" ] || continue
        words=$((words + 1))
        set -- $(python3 "$work/cmp.py" "$work/ours" "$work/theirs") "$@"
        same=$((same + $1)); tot=$((tot + $2))
        [ "$1" -eq "$2" ] && ok=$((ok + 1))
        shift 2
    done
    echo "$ok $words $same $tot"
}

fail=0
for spec in "ENG 7ad20 7af20 dog cat house water number people mother father book table chair door window bread milk cheese street school large small woman man hand foot head eye nose mouth hair day night light warm cold a e i o u" \
            "DUT 26866 26a66 hond kat huis water moeder vader kinderen bloem boek tafel stoel deur raam brood melk kaas straat fiets school groot klein vrouw kind man jongen meisje hand voet hoofd oog oor neus mond haar dag nacht licht donker warm koud a e i o u" \
            "FRN 1c582 1c782 chien chat maison eau mere pere enfant fleur livre table chaise porte fenetre pain lait fromage rue ecole grand petit femme homme main pied tete oeil nez bouche cheveux jour nuit lumiere chaud froid a e i o u" \
            "GRM 3304e 3324e hund katze haus wasser mutter vater kinder blume ende bitte sonne liebe affe garten bruder schwester freund baum name leben salat hat brot braun frei frau schnell gut gross klein alt neu tisch stuhl fenster milch strasse schule buch berg fluss stadt land himmel erde mond stern licht nacht tag jahr a e i o u" \
            "ITL 210ce 212ce cane gatto casa acqua madre padre bambino fiore libro sedia porta finestra pane latte formaggio strada scuola grande piccolo donna uomo mano piede testa occhio naso bocca capelli giorno notte luce caldo freddo a e i o u" \
            "SPN 137ec 139ec perro gato casa agua madre padre nino flor libro mesa silla puerta ventana pan leche queso calle escuela grande pequeno mujer hombre mano pie cabeza ojo nariz boca pelo dia noche luz calor frio a e i o u"; do
    set -- $(run_lang $spec)
    name=$(echo $spec | cut -d' ' -f1)
    echo "ne98test: $name $1 of $2 words, $3 of $4 frames"
    [ "$1" -eq "$2" ] || fail=1
    [ "$2" -gt 0 ] || fail=1
done

[ "$fail" -eq 0 ]
