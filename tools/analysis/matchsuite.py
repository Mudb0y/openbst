#!/usr/bin/env python3
"""Pairs the tables of two builds by the probes that reach them.

Each side is a suite run: an address, the set of probe texts that made the
build read it, and the instruction that did the reading. A table is the same
table in both builds when the same texts reach it, so identical probe sets
pair off. Where several tables share a probe set the instruction's shape
separates them, and where that is not enough the pair is reported as
ambiguous rather than guessed.
"""

import collections
import re
import sys


def read(path):
    rows = []
    names = []
    for line in open(path):
        if line.startswith("#"):
            names = line[1:].split()
            continue
        m = re.match(r"([0-9a-f]{8}) ([0-9a-f]{8})\s{2}(.{1,40}?)\s{2,}(.*)", line)
        if m:
            rows.append((int(m.group(1), 16), int(m.group(2), 16),
                         m.group(3).strip()))
    return names, rows


def shape(s):
    """The instruction with every number taken out, so that two builds reading
    the same table the same way look the same."""
    s = re.sub(r"0x[0-9a-f]+", "K", s)
    s = re.sub(r"\b\d+\b", "K", s)
    s = re.sub(r"\b(e?[abcd]x|e?[sd]i|e?bp|[abcd][lh]|es|ds|ss|cs)\b", "r", s)
    return s


def main():
    if len(sys.argv) < 3:
        print("usage: matchsuite.py SUITE_A SUITE_B [KNOWN...]\n"
              "  each KNOWN is addr=name, naming a table on the A side",
              file=sys.stderr)
        return 2
    na, ra = read(sys.argv[1])
    nb, rb = read(sys.argv[2])
    if na != nb:
        print("the two suites used different probes", file=sys.stderr)
        return 1
    known = {}
    for k in sys.argv[3:]:
        a, _, n = k.partition("=")
        known[int(a, 16)] = n

    bya = collections.defaultdict(list)
    byb = collections.defaultdict(list)
    for a, m, h in ra:
        bya[(m, shape(h))].append(a)
    for b, m, h in rb:
        byb[(m, shape(h))].append(b)

    paired, ambiguous, alone = [], [], []
    for key, aa in sorted(bya.items()):
        bb = byb.get(key)
        if not bb:
            alone.extend(aa)
            continue
        if len(aa) == 1 and len(bb) == 1:
            paired.append((aa[0], bb[0], key))
        else:
            ambiguous.append((aa, bb, key))
    print("%d tables on the A side, %d on the B side" % (len(ra), len(rb)))
    print("%d paired, %d ambiguous groups, %d unmatched" %
          (len(paired), len(ambiguous), len(alone)))
    for a, b, key in sorted(paired, key=lambda p: p[0]):
        print("  %-14s %08x -> %08x   %s" % (known.get(a, ""), a, b, key[1][:44]))
    for aa, bb, key in ambiguous:
        print("  ambiguous %s <-> %s   %s"
              % (",".join("%08x" % x for x in aa),
                 ",".join("%08x" % x for x in bb), key[1][:40]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
