#!/usr/bin/env python3
"""Checks the whole middle of the engine in one step.

Takes the sentence stream the rule pass leaves behind, runs our own pair scan
and diphone expansion over it, and requires the acoustic targets that fall out
to be the ones the engine's frame builder actually fetched.

The engine fetches only when the target changes, so runs of the same index
collapse on both sides before comparing.
"""

import subprocess
import sys

SCAN = 0x1000BC50
STREAM, LENADDR, PREV, STRONG = 0x1001E3C0, 0x1001B69C, 0x1001C008, 0x1001B628
FETCH = 0x10004130


def collapse(seq):
    return [v for i, v in enumerate(seq) if i == 0 or v != seq[i - 1]]


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        snap = "0x%x:0x%x:%d,0x%x:2,0x%x:1,0x%x:1" % (
            SCAN, STREAM, 1024, LENADDR, PREV, STRONG)
        r = subprocess.run([oracle, "--dll", dll, "--speak", text,
                            "--snap", snap, "--hook", "0x%x:2" % FETCH],
                           capture_output=True, text=True)
        states, theirs = [], []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                parts = [p.strip() for p in l[4:].split("|")]
                lo, hi = parts[1].split()
                states.append((int(hi, 16) << 8 | int(lo, 16),
                               int(parts[2], 16), int(parts[3], 16),
                               parts[0].split()))
            elif l.startswith("call "):
                theirs.append(int(l.split()[3]))
        if not states:
            continue
        inp = "".join("P %d %d %d %s\n" % (ln, prev, strong, " ".join(st))
                      for ln, prev, strong, st in states)
        q = subprocess.run([synth, dll, "-t"], input=inp,
                           capture_output=True, text=True)
        ours = [int(x) for x in q.stdout.split() if x != "#"]
        if collapse(ours) == collapse(theirs):
            same += 1
        else:
            diff += 1
            if shown < 3:
                shown += 1
                print("  DIFFER: %s" % text)
                print("    ours  : %s" % " ".join(map(str, collapse(ours)[:24])))
                print("    theirs: %s" % " ".join(map(str, collapse(theirs)[:24])))
    print("targettest: %d sentences identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
