#!/usr/bin/env python3
"""Lines two language modules' code up and reports every constant that moved.

The six 1998 modules are one body of code compiled once per language, so a
function in one has the same instructions as the function in the other and
differs only in its immediates. Matching functions by the shape of their
instructions and then walking the two in step gives, for every literal, what
the other build put there.

That is the translation between the two phoneme numberings, taken from the
compiler's own output rather than guessed at from the tables.
"""

import collections
import difflib
import re
import struct
import sys

import capstone

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
PROLOGUES = (rb"\x8c\xd8\x90\x45\x55\x8b\xec", rb"\x55\x8b\xec")


def functions(code):
    starts = set()
    for pat in PROLOGUES:
        for m in re.finditer(pat, code):
            starts.add(m.start())
    return sorted(starts)


def body(code, start, stop):
    shapes, lits, disps = [], [], []
    for ins in MD.disasm(code[start:stop], start):
        shapes.append("%s %s" % (ins.mnemonic,
                                 re.sub(r"0x[0-9a-f]+", "K", ins.op_str)))
        lits.append([int(m.group(1), 16)
                     for m in re.finditer(r"0x([0-9a-f]+)", ins.op_str)])
        disps.append([int(m.group(1), 16)
                      for m in re.finditer(r"0x([0-9a-f]+)", ins.op_str)
                      if int(m.group(1), 16) >= 0x40])
        if ins.mnemonic in ("ret", "retf"):
            break
    return shapes, lits, disps


def load(path):
    code = open(path, "rb").read()
    fs = functions(code)
    out = {}
    for i, f in enumerate(fs):
        stop = fs[i + 1] if i + 1 < len(fs) else len(code)
        stop = min(stop, f + 0x600)
        sh, li, di = body(code, f, stop)
        if len(sh) >= 6:
            out[f] = (sh, li, di)
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: codediff.py A.bin B.bin [--all]", file=sys.stderr)
        return 2
    a, b = load(sys.argv[1]), load(sys.argv[2])
    ia = collections.defaultdict(list)
    ib = collections.defaultdict(list)
    for f, (sh, _, _) in a.items():
        ia[tuple(sh)].append(f)
    for g, (sh, _, _) in b.items():
        ib[tuple(sh)].append(g)

    votes = collections.defaultdict(collections.Counter)
    addrs = collections.defaultdict(collections.Counter)
    pairs = 0
    for sh, fs in ia.items():
        gs = ib.get(sh)
        if not gs or len(fs) != 1 or len(gs) != 1 or len(sh) < 8:
            continue
        pairs += 1
        la, lb = a[fs[0]][1], b[gs[0]][1]
        da, db = a[fs[0]][2], b[gs[0]][2]
        for xa, xb in zip(da, db):
            if len(xa) == len(xb):
                for va, vb in zip(xa, xb):
                    addrs[va][vb] += 1
        for xa, xb in zip(la, lb):
            if len(xa) != len(xb):
                continue
            for va, vb in zip(xa, xb):
                if va <= 0xFF and vb <= 0xFF:
                    votes[va][vb] += 1

    # How far the marks moved: the difference the constants above the sound
    # inventory agree on.
    d = collections.Counter()
    for va, c in votes.items():
        if va < 0x31:
            continue
        for vb, k in c.items():
            if vb != va:
                d[vb - va] += k
    print("%d functions in A, %d in B, %d matched by shape" % (len(a), len(b), pairs))
    if d:
        (sh, k), = d.most_common(1)
        print("mark shift: %+d (%d of %d moved constants agree)"
              % (sh, k, sum(d.values())))
    print("constants that moved:")
    for va in sorted(votes):
        c = votes[va]
        (vb, n), = c.most_common(1)
        if vb == va and len(c) == 1 and "--all" not in sys.argv:
            continue
        other = " ".join("%02x:%d" % (k, v) for k, v in c.most_common(4))
        print("  %02x -> %-3s  (%s)" % (va, "%02x" % vb if n * 2 > sum(c.values()) else "?",
                                        other))
    want = [x for x in sys.argv[3:] if x.startswith("0x")]
    if want:
        print("addresses:")
        for w in want:
            v = int(w, 16)
            c = addrs.get(v)
            if not c:
                print("  %04x -> not seen" % v)
                continue
            (vb, n), = c.most_common(1)
            print("  %04x -> %04x  (%s)" %
                  (v, vb, " ".join("%04x:%d" % t for t in c.most_common(3))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
