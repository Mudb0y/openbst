#!/usr/bin/env python3
"""Checks frame generation against the engine.

Captures the three record streams the front end leaves behind, runs our own
generator over them, and requires the sixteen-byte frames to match the ones
the engine produced.
"""

import subprocess
import sys

AT = 0x10004C84
SEG, NSEG = 0x100193B0, 0x1001B764
TRN, NTRN = 0x10019A90, 0x1001C000
ITO, NITO = 0x1001A6C0, 0x1001C012
RATE, VOICE, FLAGS = 0x1001A8B6, 0x10019A5A, 0x1001B632
DEFEXC, GBASE, GADJ = 0x10019308, 0x10019398, 0x1001939C
SEGRD, TRNBASE, TRNPEND, ITORD = 0x10019A60, 0x1001B62E, 0x1001BFF4, 0x10019A70


def le(parts, signed=False):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    if signed and v >= 1 << (8 * len(parts) - 1):
        v -= 1 << (8 * len(parts))
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    fsame = ftot = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        r = subprocess.run(
            [oracle, "--dll", dll, "--frames", text,
             "--snap", "0x%x:0x%x:%d,0x%x:2,0x%x:%d,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2"
                       % (AT, SEG, 0xD0 * 8, NSEG, TRN, 0x210 * 6, NTRN,
                          SEGRD, TRNBASE, TRNPEND, ITORD),
             "--snap", "0x%x:0x%x:%d,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2,0x%x:2"
                       % (AT, ITO, 0x60 * 6, NITO, RATE, VOICE, FLAGS,
                          DEFEXC, GBASE, GADJ)],
            capture_output=True, text=True)
        runs, theirs = [], []
        pending = {}
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                pending["seg"] = p[0][:le(p[1]) * 8]
                pending["trn"] = p[2][:le(p[3]) * 6]
                pending["c"] = (le(p[4], True), le(p[5], True),
                                le(p[6], True), le(p[7], True))
            elif l.startswith("U | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                pending["ito"] = p[0][:le(p[1]) * 6]
                pending["v"] = (le(p[3], True), le(p[2], True), le(p[4]),
                                le(p[5], True), le(p[6], True), le(p[7], True))
                runs.append(dict(pending))
            elif l.startswith("F "):
                f = l.split()
                for i in range(1, len(f), 17):
                    theirs.append(f[i:i + 16])
        if not runs:
            continue
        inp = ""
        for g in runs:
            voice, rate, flags, defexc, gb, ga = g["v"]
            segrd, trnbase, trnpend, itord = g["c"]
            inp += "V %d %d %d %d %d %d %d %d %d %d\n" % (
                voice, rate, flags, defexc, gb, ga, segrd, trnbase, trnpend, itord)
            inp += "S %s\n" % " ".join(g["seg"])
            inp += "T %s\n" % " ".join(g["trn"])
            inp += "I %s\n" % " ".join(g["ito"])
            inp += "\n"
        q = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
        ours = [l.split() for l in q.stdout.splitlines() if l and l[0] != "#"]
        m = max(len(theirs), len(ours))
        ftot += m
        for i in range(m):
            if i < len(theirs) and i < len(ours) and theirs[i] == ours[i]:
                fsame += 1
        if theirs == ours:
            same += 1
        else:
            diff += 1
            if shown < 3:
                shown += 1
                print("  DIFFER: %s  (engine %d frames, ours %d)"
                      % (text, len(theirs), len(ours)))
                for i in range(m):
                    a = " ".join(theirs[i]) if i < len(theirs) else "-"
                    b = " ".join(ours[i]) if i < len(ours) else "-"
                    if a != b:
                        print("    [%d] engine %s" % (i, a))
                        print("        ours   %s" % b)
                        break
    print("frametest: %d utterances identical, %d differing; %d/%d frames"
          % (same, diff, fsame, ftot))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
