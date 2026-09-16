# BeSTspeech

A from-scratch reimplementation of the Berkeley Speech Technologies formant
synthesizer and text-to-speech front end, the engine sold as BeSTspeech and
shipped in HumanWare's Keynote GOLD. It builds anywhere a C compiler runs, it
speaks twenty builds across thirteen languages, and it is measured against the
original binaries sample for sample rather than by ear.

Nothing of the original code is used. The tables are, and where they came from
is set out under Licence below.

## What it speaks

Three generations of the engine, twenty builds in all.

The 1995 32-bit build, English. The six 1998 16-bit modules: English, Dutch,
French, German, Italian and Spanish. The thirteen 2006 32-bit builds: Arabic,
Dutch, English, French, German, Greek, Hebrew, Italian, Japanese, Polish,
Portuguese, Russian and Spanish.

`bstspeak --list` names them. The 1995 and 1998 builds run at 11025 samples a
second, the 2006 builds at 10000, except Russian at 10800. Greek, Arabic,
Hebrew and Russian read text in their own code pages, which is what their
originals did.

## How exact it is

Every claim here is a test in `tests`, and every test compares against the
original binary running under an emulator in `tools/oracle`.

The 1995 build is bit-exact: 24 utterances identical through the whole
pipeline, and identical again stage by stage through the cursors, the
letter-to-sound pass, the stress pass, the assembler, the phoneme rules, the
accent pass, the pair scan, the contour, the segment targets, the frames, the
interpolator and the lattice.

The six 1998 modules are frame for frame exact: 226 words, 20793 frames, none
differing.

The thirteen 2006 builds are exact on a sentence each, on 728 numbers, on a
hundred words each for the twelve non-Russian builds, and on fifty Russian
words, three sentences and fifty-four numbers. That is 2048 utterances with
none differing.

The tables compiled into the library are checked against the tables in the
binaries by `tests/lifttest.sh`, over every word of every corpus, twenty
thousand words the lift never saw, four hundred numbers and three sentences:
411864 utterances across the twenty builds, none differing.

All of that needs the original binaries. Two tests do not. `make selftest`
runs the library against `tests/golden.txt`, which carries, for 4200
utterances across the twenty builds, how many samples came out and a hash of
them. Those numbers were written from the binaries rather than from the
library, so a clone with no binaries can still hold the library to what the
originals said. `tests/tabletest.sh` checks every build's lattice tables
against the 1995 build's: the excitation, the gain and the log pair are the
same numbers in all twenty and only their place in the file differs, so a
build whose offsets are wrong reads something that does not match.

## Building

    make

That writes `build/libbst.a`, `build/libbst.so` and `build/bstspeak`. There
are no dependencies beyond a C compiler and make; it is built here with both
gcc and clang, which produce the same samples.

`make install PREFIX=/usr/local` installs the library, `bst.h` and the
command. With Nix, `nix build` builds the same thing and runs the self-test on
the way, `nix run .` is `bstspeak`, and `nix develop` is the shell the
analysis tools want.

## Using it

    bstspeak --build 2006GER "Guten Tag, wie geht es Ihnen?" > hello.wav
    bstspeak --list
    bstspeak --build 1995 --pitch 0x40 --raw < text.txt | aplay -f S16_LE -r 11025

From C, with `include/bst.h` and `libbst`:

```c
#include "bst.h"

bst *h = bst_open("2006ENG");
bst_set(h, "pitch", 0x50);

long n = bst_length(h, "Hello there.");
int16_t *pcm = malloc(n * sizeof *pcm);
bst_say(h, "Hello there.", pcm, n);
/* n samples, signed 16 bit, one channel, at bst_rate(h) hertz */

bst_close(h);
```

The whole interface is ten calls: `bst_builds`, `bst_open`,
`bst_open_image`, `bst_open_images`, `bst_close`, `bst_rate`, `bst_set`,
`bst_get`, `bst_length` and `bst_say`. The settings `bst_set` takes are
`pitch`, `top`, `level`, `voice` and `rate`. `bst_open_image` opens a build
over a caller's copy of an original binary instead of the tables compiled in,
which is what the lift and its test compare against; the six 1998 builds keep
their excitation and gain tables in the core module all of them share, so
those need `bst_open_images` with that module as well.

## Licence

Two things under two terms, and the split matters.

The code, the tests and the tools are ours, under MIT.

The tables are not. Most of them are the files under `src/data`; a handful of
smaller ones are written out inside `src/text.c`. They are read out of
Berkeley Speech Technologies' binaries, and they are Berkeley Speech
Technologies' work and later HumanWare's. We are in no position to license
them to anyone. See `LICENSE`.

## The tables, and why the tests need binaries we cannot ship

`tools/lift` takes the tables out of a binary by watching every read the
engine makes while it says a wide corpus, and then keeping whole the sections
those reads landed in. Whole sections rather than only the bytes seen: a
dictionary record is read only for a word that is in the dictionary, so
coverage alone would quietly drop most of one and leave those words to the
rules. What it leaves behind is the engine's own code, which is never read.
That is 3.6 megabytes of tables out of the 5.1 megabytes of binary the
twenty builds come to.

`src/data` is checked in, so building and using the library needs nothing
else, and `make selftest` and `tests/tabletest.sh` will run. The other thirty
tests compare against an original binary under the emulator, and those binaries are not in this
repository and will not be. Put them under `dll/1995`, `dll/1998` and
`dll/2006` and `make test` will run them all; without them only the self-test
can.

`make golden` writes `tests/golden.txt` again from the binaries. It is the
one thing in the repository written by the originals rather than checked
against them, which is what lets the self-test stand in for the rest.

`make lift` writes `src/data` again from those binaries. What is checked in
is already what it produces.

## Layout

`include` is the public header and the internal ones. `src` is the engine, a
file to a stage, with `src/speak.c` as the front end over them and `src/data`
as the tables. `tests` is thirty-two scripts. `tools/synth` builds the library and
its drivers, `tools/oracle` runs the original binaries to compare against, and
`tools/analysis` holds the scripts that found the tables in the first place.
`.github/workflows/ci.yml` builds with both compilers, runs the self-test,
installs the library and links a program against it both ways, and builds the
flake. It cannot run the other thirty tests, for want of the binaries.
