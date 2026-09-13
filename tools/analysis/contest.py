#!/usr/bin/env python3
"""Checks the intonation contour against the engine's own records."""

import subprocess
import sys

GEN, PUSH = 0x100129C0, 0x10002A20
STREAM, LENADDR = 0x1001E3C0, 0x1001B69C
BASE, TOP, LEVEL, VOICE = 0x10019A48, 0x1001A6B6, 0x10019A4C, 0x1001C00A
N = 512


def le(parts):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    recs_same = recs_tot = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        r = subprocess.run(
            [oracle, "--dll", dll, "--speak", text,
             "--snap", "0x%x:0x%x:%d,0x%x:2,0x%x:2,0x%x:2,0x%x:1,0x%x:2"
             % (GEN, STREAM, N, LENADDR, BASE, TOP, LEVEL, VOICE),
             "--hook", "0x%x:4" % PUSH],
            capture_output=True, text=True)
        groups = []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip() for x in l[4:].split("|")]
                groups.append(((le(p[1].split()), le(p[2].split()),
                                le(p[3].split()), le(p[4].split()),
                                le(p[5].split())), p[0].split(), []))
            elif l.startswith("call ") and groups:
                a = [int(x) for x in l.split()[2:]]
                groups[-1][2].append((a[0] & 0xFF, a[1] & 0xFF,
                                      a[2] & 0xFFFF, a[3] & 0xFFFF))
        if not groups:
            continue
        inp = "".join("C %d %d %d %d %d %s\n"
                      % (st[0], st[1], st[2], st[3], st[4], " ".join(stream))
                      for st, stream, _ in groups)
        q = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
        mine, curr = [], None
        for l in q.stdout.splitlines():
            if l == "#":
                curr = []
                mine.append(curr)
            elif curr is not None:
                f = [int(x) for x in l.split()]
                curr.append((f[0], f[1], f[2] & 0xFFFF, f[3] & 0xFFFF))
        for (st, stream, theirs), ours in zip(groups, mine):
            m = max(len(theirs), len(ours))
            recs_tot += m
            for i in range(m):
                x = theirs[i] if i < len(theirs) else None
                y = ours[i] if i < len(ours) else None
                if x == y:
                    recs_same += 1
            if theirs == ours:
                same += 1
            else:
                diff += 1
                if shown < 4:
                    shown += 1
                    print("  DIFFER: %s  (engine %d, ours %d)"
                          % (text, len(theirs), len(ours)))
                    for i in range(m):
                        x = theirs[i] if i < len(theirs) else None
                        y = ours[i] if i < len(ours) else None
                        if x != y:
                            print("    [%d] engine %s ours %s" % (i, x, y))
                            break
    print("contest: %d contours identical, %d differing; %d/%d records"
          % (same, diff, recs_same, recs_tot))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
