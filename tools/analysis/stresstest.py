#!/usr/bin/env python3
"""Checks word stress against the engine.

Captures each word's code stream on the way into the stress pass and on the
way out, and requires our pass to turn one into the other.
"""

import subprocess
import sys

IN, OUT = 0x1001184C, 0x10011851
BUF, EMPH, MODE = 0x10019330, 0x10019A5E, 0x1001B46D


def le(parts):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        r = subprocess.run(
            [oracle, "--dll", dll, "--speak", text,
             "--snap", "0x%x:0x%x:64,0x%x:1,0x%x:1" % (IN, BUF, EMPH, MODE),
             "--snap", "0x%x:0x%x:64" % (OUT, BUF)],
            capture_output=True, text=True)
        ins, outs = [], []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                ins.append((le(p[1]), le(p[2]), p[0]))
            elif l.startswith("U | "):
                outs.append(l[4:].strip().split())
        if not ins or len(ins) != len(outs):
            continue
        inp = "".join("W %d %d %s\n" % (e, m, " ".join(b)) for e, m, b in ins)
        q = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
        mine = [l.split() for l in q.stdout.splitlines()]
        for want, got in zip(outs, mine):
            if want == got:
                same += 1
            else:
                diff += 1
                if shown < 5:
                    shown += 1
                    print("  DIFFER: %s" % text)
                    print("    engine %s" % " ".join(want[:24]))
                    print("    ours   %s" % " ".join(got[:24]))
    print("stresstest: %d words identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
