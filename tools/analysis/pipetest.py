#!/usr/bin/env python3
"""The whole chain against the engine's audio.

Captures the sentence phoneme stream on the way into the accent pass, together
with the voice state, and runs everything after that in our own code: accents,
the pair scan, the contour, frame generation and the lattice. The samples must
match the engine's, byte for byte.
"""

import os
import subprocess
import sys
import tempfile

AT = 0x10013180
STREAM, LENADDR = 0x1001E3C0, 0x1001B69C
FLAGS, CARRIED, LEVEL, TAILPTR = 0x1001B46D, 0x100193A1, 0x10019A4C, 0x1001E3B0
BASE, TOP, VOICE, RATE = 0x10019A48, 0x1001A6B6, 0x10019A5A, 0x1001A8B6
GFLAGS, DEFEXC, GBASE, GADJ = 0x1001B632, 0x10019308, 0x10019398, 0x1001939C
PREV, STRONG = 0x1001C008, 0x1001B628
N = 512


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
             "--snap", "0x%x:0x%x:%d,0x%x:2,0x%x:1,0x%x:1,0x%x:1,0x%x:4,0x%x:2,0x%x:2"
                       % (AT, STREAM, N, LENADDR, FLAGS, CARRIED, LEVEL,
                          TAILPTR, BASE, TOP),
             "--snap", "0x%x:0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:1,0x%x:1"
                       % (AT, VOICE, RATE, GFLAGS, DEFEXC, GBASE, GADJ, PREV, STRONG)],
            capture_output=True, text=True)
        pcm = open(pcmfile, "rb").read()
        lines = [m for m in r.stdout.splitlines()
                 if m.startswith("S | ") or m.startswith("U | ")]
        pending, runs = {}, []
        for l in lines:
            p = [x.strip().split() for x in l[4:].split("|")]
            if l.startswith("S | "):
                pending = {
                    "stream": p[0], "len": le(p[1]), "flags": le(p[2]),
                    "carried": le(p[3]), "level": le(p[4], True),
                    "tail": le(p[5]) + 3 - STREAM, "base": le(p[6], True),
                    "top": le(p[7], True)}
            else:
                pending.update({
                    "voice": le(p[0], True), "rate": le(p[1], True),
                    "gflags": le(p[2]), "defexc": le(p[3], True),
                    "gb": le(p[4], True), "ga": le(p[5], True),
                    "prev": le(p[6]), "strong": le(p[7])})
                runs.append(dict(pending))
        if not runs:
            continue
        inp = "".join(
            "P %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s\n"
            % (g["len"], g["flags"], g["carried"], g["level"], g["tail"],
               g["base"], g["top"], g["voice"], g["rate"], g["gflags"],
               g["defexc"], g["gb"], g["ga"], g["prev"], g["strong"],
               " ".join(g["stream"]))
            for g in runs)
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
    print("pipetest: %d utterances identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
