#!/usr/bin/env python3
"""Places a table in another build by the instruction that reads it.

The global vote xmap takes over every aligned instruction gets some tables
wrong: two tables that sit a fixed distance apart are named by the same
arithmetic, and the wrong one wins. This narrows the question. A run of the
English build under the emulator says which instruction reads which table;
pairing that one function and lining the two up says what the same
instruction names in the other build, and nothing else votes.

Usage: tabsite.py READS.BIN ENGLISH_DLL DLL FIELD=ADDR[,SPAN]...
"""

import collections
import difflib
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import xmap


def sites(path, lo, hi):
    """The instruction addresses that read inside [lo, hi)."""
    d = open(path, "rb").read()
    out = collections.Counter()
    for i in range(len(d) // 12):
        a, pc, _s = struct.unpack_from("<IIB", d, i * 12)
        if lo <= a < hi:
            out[pc] += 1
    return out


def owner(starts, pc):
    c = [x for x in starts if x <= pc]
    return max(c) if c else None


def main():
    if len(sys.argv) < 5:
        print(__doc__, file=sys.stderr)
        return 2
    reads, enpath, path = sys.argv[1:4]
    ta, ra = xmap.load(enpath)
    tb, rb = xmap.load(path)
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
    sig = xmap.match_signatures(fa, fb)
    known = xmap.propagate(fa, fb, xmap.pairs_from_matches(fa, fb, sig))
    import immdiff
    pair = immdiff.fn_pairs(fa, fb, sig, known)
    starts = sorted(fa)

    for spec in sys.argv[4:]:
        field, rest = spec.split("=")
        parts = rest.split(",")
        base = int(parts[0], 0)
        span = int(parts[1], 0) if len(parts) > 1 else 0x40
        votes = collections.Counter()
        for pc, n in sites(reads, base, base + span).items():
            f = owner(starts, pc)
            g = pair.get(f) if f else None
            if g is None or g not in fb:
                continue
            siga, bodya = fa[f]
            sigb, bodyb = fb[g]
            ka = [" ".join("i" if t.startswith("i") else t for t in x.split())
                  for x in siga]
            kb = [" ".join("i" if t.startswith("i") else t for t in x.split())
                  for x in sigb]
            sm = difflib.SequenceMatcher(a=ka, b=kb, autojunk=False)
            for i, j, m in sm.get_matching_blocks():
                for k in range(m):
                    xa, xb = bodya[i + k][1], bodyb[j + k][1]
                    if len(xa) != len(xb):
                        continue
                    for u, v in zip(xa, xb):
                        if -0x10 <= u - base < span:
                            votes[v - (u - base)] += n
        if votes:
            best, n = votes.most_common(1)[0]
            print("%-14s 0x%08X  (%d of %d)" % (field, best, n, sum(votes.values())))
        else:
            print("%-14s not placed" % field)
    return 0


if __name__ == "__main__":
    sys.exit(main())
