#!/usr/bin/env python3
"""Checks the pair scan against the engine's own emissions.

For each sentence the engine runs the scan once per phrase. This captures the
stream and the carried state at each entry, together with every segment and
transition the scan pushes, and requires our records to match one for one.

Segments carry the count of transitions since the last one, so comparing the
two record kinds in a single ordered sequence checks the interleaving as well
as the values.
"""

import re
import subprocess
import sys

SCAN = 0x1000BC50
STREAM, LENADDR, PREV, STRONG = 0x1001E3C0, 0x1001B69C, 0x1001C008, 0x1001B628
SEG, TRANS = 0x10002910, 0x100029A0


def capture(oracle, dll, text):
    """Returns a list of (state, records) pairs, one per scan invocation."""
    snap = "0x%x:0x%x:%d,0x%x:2,0x%x:1,0x%x:1" % (
        SCAN, STREAM, 256, LENADDR, PREV, STRONG)
    hooks = "0x%x:4,0x%x:4" % (SEG, TRANS)
    r = subprocess.run([oracle, "--dll", dll, "--speak", text,
                        "--snap", snap, "--hook", hooks],
                       capture_output=True, text=True)
    groups = []
    for line in r.stdout.splitlines():
        if line.startswith("S | "):
            parts = [p.strip() for p in line[4:].split("|")]
            stream = parts[0].split()
            ln = int(parts[1].split()[1], 16) << 8 | int(parts[1].split()[0], 16)
            prev = int(parts[2], 16)
            strong = int(parts[3], 16)
            groups.append(((ln, prev, strong, stream), []))
        elif line.startswith("call ") and groups:
            f = line.split()
            pc = int(f[1], 16)
            args = [int(x) for x in f[2:]]
            if pc == SEG:
                groups[-1][1].append(("S", args[1] & 0xFFFF, args[0] & 0xFFFF))
            else:
                groups[-1][1].append(("T", args[0] & 0xFFFF, args[1] & 0xFFFF,
                                      sb(args[2]), sb(args[3])))
    return groups


def sb(v):
    v &= 0xFF
    return v - 256 if v > 127 else v


def ours(synth, dll, states):
    inp = "".join("P %d %d %d %s\n" % (ln, prev, strong, " ".join(st))
                  for ln, prev, strong, st in states)
    r = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
    out, cur = [], None
    for line in r.stdout.splitlines():
        if line == "#":
            cur = []
            out.append(cur)
        elif cur is not None:
            f = line.split()
            if f[0] == "S":
                cur.append(("S", int(f[1]), int(f[2])))
            else:
                cur.append(("T", int(f[1]), int(f[2]), int(f[3]), int(f[4])))
    return out


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    recs_same = recs_tot = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        groups = capture(oracle, dll, text)
        if not groups:
            continue
        mine = ours(synth, dll, [g[0] for g in groups])
        for (state, theirs), ourrecs in zip(groups, mine):
            n = max(len(theirs), len(ourrecs))
            recs_tot += n
            for i in range(n):
                a = theirs[i] if i < len(theirs) else None
                b = ourrecs[i] if i < len(ourrecs) else None
                if a == b:
                    recs_same += 1
            if theirs == ourrecs:
                same += 1
            else:
                diff += 1
                if shown < 3:
                    shown += 1
                    print("  DIFFER: %s  (engine %d records, ours %d)"
                          % (text, len(theirs), len(ourrecs)))
                    for i in range(n):
                        a = theirs[i] if i < len(theirs) else None
                        b = ourrecs[i] if i < len(ourrecs) else None
                        if a != b:
                            print("    [%d] engine %s ours %s" % (i, a, b))
                            break
    print("pairtest: %d scans identical, %d differing; %d/%d records"
          % (same, diff, recs_same, recs_tot))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
