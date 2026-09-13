#!/usr/bin/env python3
"""Everything after the tokeniser against the engine's audio.

Captures the token sequence and the voice state, runs assembly, the rule pass,
accents, the pair scan, the contour, frame generation and the lattice in our
own code, and requires the samples to match the engine's.
"""

import os
import subprocess
import sys
import tempfile

TOK = 0x1000E49D
KIND, BUF, EMPH, MODE = 0x1001C036, 0x10019330, 0x10019A5E, 0x1001B46D
BASE, TOP, LEVEL, VOICE, RATE = 0x10019A48, 0x1001A6B6, 0x10019A4C, 0x10019A5A, 0x1001A8B6
GFLAGS, DEFEXC, GBASE, GADJ = 0x1001B632, 0x10019308, 0x10019398, 0x1001939C


def le(parts, signed=False):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    if signed and v >= 1 << (8 * len(parts) - 1):
        v -= 1 << (8 * len(parts))
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    tmp = tempfile.mkdtemp()
    same = diff = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        pcmfile = os.path.join(tmp, "ref.pcm")
        r = subprocess.run(
            [oracle, "--dll", dll, "--speak", text, "--raw", "--out", pcmfile,
             "--snap", "0x%x:0x%x:1,0x%x:96,0x%x:1,0x%x:1"
                       % (TOK, KIND, BUF, EMPH, MODE),
             "--snap", "0x%x:0x%x:2,0x%x:2,0x%x:1,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2"
                       % (TOK, BASE, TOP, LEVEL, VOICE, RATE, GFLAGS, GBASE, GADJ)],
            capture_output=True, text=True)
        pcm = open(pcmfile, "rb").read()
        toks, vstate = [], None
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                toks.append((le(p[0]), le(p[2]), le(p[3]), p[1]))
            elif l.startswith("U | ") and vstate is None:
                p = [x.strip().split() for x in l[4:].split("|")]
                vstate = (le(p[0], True), le(p[1], True), le(p[2], True),
                          le(p[3], True), le(p[4], True), le(p[5]),
                          le(p[6], True), le(p[7], True))
        if not toks or vstate is None:
            continue
        base, top, level, vsel, rate, gflags, gb, ga = vstate
        inp = "V %d %d %d %d %d %d %d %d %d\n" % (
            base, top, level, vsel, rate, gflags, 0x30, gb, ga)
        for kind, emph, mode, buf in toks:
            inp += "K %d %d %d %s\n" % (kind, emph, mode, " ".join(buf))
        q = subprocess.run([synth, dll], input=inp.encode(), capture_output=True)
        if q.stdout == pcm:
            same += 1
        else:
            diff += 1
            if shown < 3:
                shown += 1
                bad = next((i for i in range(min(len(pcm), len(q.stdout)))
                            if pcm[i] != q.stdout[i]), None)
                print("  DIFFER: %s  (engine %d bytes, ours %d, first at %s)"
                      % (text, len(pcm), len(q.stdout), bad))
    print("fulltest: %d utterances identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
