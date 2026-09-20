# openbst

A from-scratch reimplementation of the Berkeley Speech Technologies formant
synthesizer, the engine sold as BeSTspeech and shipped in HumanWare's Keynote
GOLD. Twenty builds across thirteen languages in three generations. `README.md`
says what it is, what it speaks, how exact it is and how it is laid out; read it
before touching anything, and keep it true when something moves.

The directory is still called `reb32`. The project is openbst.

## Bit exactness is the standard here

Unlike a project where a changed case is a question, a changed sample here is a
failure. The 1995 build is bit-exact through every stage, the six 1998 modules
are frame for frame and sample for sample exact, and the thirteen 2006 builds are
exact over 2358 utterances. Nothing is allowed to move. If a test differs, the
change is wrong until proven otherwise, and the burden is never on the test.

gcc and clang must produce the same samples. A change that makes them disagree is
a bug in the change.

## The gate

`make test` is thirty-four scripts and is the real gate, but thirty-one of them need
the original binaries, which are not in this repository and never will be. They
go under `dll/1995`, `dll/1998` and `dll/2006`.

Without those binaries only `make selftest` and `tests/tabletest.sh` can run.
`make selftest` holds the library to `tests/golden.txt`, which records the sample
count and a hash for 4906 utterances across the twenty builds.

## Two files that must not be casually regenerated

`tests/golden.txt` is the one thing in the repository written by the originals
rather than checked against them. `make golden` rewrites it from the binaries.
Running that after a change does not verify the change, it launders it: whatever
the library now does becomes the new standard. Only regenerate golden when the
binaries themselves are the thing that changed, and say so explicitly in the
commit.

`src/data` holds the lifted tables and is checked in. `make lift` rewrites it,
and what is checked in is already what it produces, so there is no routine reason
to run it.

## Licence split

The code, the tests and the tools are ours under MIT. The tables are not: most of
`src/data`, plus a handful written out inside `src/text.c`, are read out of
Berkeley Speech Technologies' binaries and are their work and later HumanWare's.
Never put an MIT header on anything in `src/data`, and never write anything
implying MIT reaches it. `LICENSE` is the file that says whose is whose.

## Speech is life-support

Stas is blind and this machine's speech stack is how he reads errors. Nothing
here may reconfigure, restart or kill PipeWire, speech-dispatcher, Orca or
espeakup. Playing audio as an ordinary client is fine; touching the servers is
not.
