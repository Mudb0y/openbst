#!/usr/bin/env python3
"""Checks the accent assignment against the engine.

Captures the sentence stream on the way into the pass and on the way into the
pair scan, which is the next thing to touch it, and requires our own pass to
turn the first into the second.
"""

import subprocess
import sys

IN, OUT = 0x10013180, 0x1000BC50
STREAM, LENADDR = 0x1001E3C0, 0x1001B69C
FLAGS, CARRIED, LEVEL, TAILPTR = 0x1001B46D, 0x100193A1, 0x10019A4C, 0x1001E3B0
N = 512


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
             "--snap", "0x%x:0x%x:%d,0x%x:2,0x%x:1,0x%x:1,0x%x:1,0x%x:4"
             % (IN, STREAM, N, LENADDR, FLAGS, CARRIED, LEVEL, TAILPTR),
             "--snap", "0x%x:0x%x:%d" % (OUT, STREAM, N)],
            capture_output=True, text=True)
        ins, outs = [], []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip() for x in l[4:].split("|")]
                lo, hi = p[1].split()
                ln = int(hi, 16) << 8 | int(lo, 16)
                ptr = p[5].split()
                tail = (int(ptr[3], 16) << 24 | int(ptr[2], 16) << 16 |
                        int(ptr[1], 16) << 8 | int(ptr[0], 16)) + 3 - STREAM
                ins.append((ln, int(p[2], 16), int(p[3], 16), int(p[4], 16),
                            tail, p[0].split()))
            elif l.startswith("U | "):
                outs.append(l[4:].strip().split())
        if not ins or len(ins) != len(outs):
            continue
        inp = "".join("A %d %d %d %d %d %s\n" % (ln, fl, ca, lv, tl, " ".join(st))
                      for ln, fl, ca, lv, tl, st in ins)
        q = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
        mine = [l.split() for l in q.stdout.splitlines()]
        for k, (want, got) in enumerate(zip(outs, mine)):
            if want == got:
                same += 1
            else:
                diff += 1
                if shown < 4:
                    shown += 1
                    bad = [i for i in range(min(len(want), len(got)))
                           if want[i] != got[i]]
                    print("  DIFFER: %s  [stream %d]" % (text, k))
                    print("    in    : %s" % " ".join(ins[k][5][:48]))
                    for i in bad[:8]:
                        print("    [%d] engine %s ours %s" % (i, want[i], got[i]))
    print("acctest: %d streams identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
