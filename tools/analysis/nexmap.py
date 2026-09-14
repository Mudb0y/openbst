#!/usr/bin/env python3
"""Carries offsets from one 1998 language module to another through the code.

The six modules are one body of code compiled once each, so a function in one
has the same instructions as the function in the other and differs only in the
offsets it names. Matching functions by the shape of their instructions and
then walking the two in step gives, for every offset one module's directory
holds, the offset the other module puts there.

Only the code segment is read, and only after its internal fixups have been
applied, because a segment number in the instruction stream means nothing
until they are.
"""

import collections
import difflib
import re
import struct
import sys

import capstone

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from mkmap import relocated

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
MD.detail = True
PROLOGUES = (rb"\x8c\xd8\x90\x45\x55\x8b\xec", rb"\x55\x8b\xec")


def code_of(path):
    d = open(path, "rb").read()
    segs, body = relocated(d)
    return bytes(body[1])


def functions(code):
    starts = set()
    for pat in PROLOGUES:
        for m in re.finditer(pat, code):
            starts.add(m.start())
    for i in range(len(code) - 3):
        if code[i] == 0xE8:
            rel = struct.unpack_from("<h", code, i + 1)[0]
            t = (i + 3 + rel) & 0xFFFF
            if t < len(code):
                starts.add(t)
    return sorted(starts)


def disasm(code, start, stop):
    out = []
    for ins in MD.disasm(code[start:stop], start):
        shape = [ins.mnemonic]
        disps = []
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_REG:
                shape.append("r%d" % op.reg)
            elif op.type == capstone.x86.X86_OP_IMM:
                shape.append("i")
            else:
                shape.append("m%d:%d" % (op.mem.base, op.mem.index))
                d = op.mem.disp & 0xFFFF
                if d >= 0x40:
                    disps.append(d)
        out.append((" ".join(shape), disps))
        if ins.mnemonic in ("ret", "retf"):
            break
    return out


def load(path):
    code = code_of(path)
    fs = functions(code)
    out = {}
    for i, f in enumerate(fs):
        stop = fs[i + 1] if i + 1 < len(fs) else len(code)
        out[f] = disasm(code, f, min(stop, f + 0x800))
    return {f: v for f, v in out.items() if len(v) >= 8}


def translate(a, b):
    ia = collections.defaultdict(list)
    ib = collections.defaultdict(list)
    for f, v in a.items():
        ia[tuple(s for s, _ in v)].append(f)
    for g, v in b.items():
        ib[tuple(s for s, _ in v)].append(g)
    votes = collections.defaultdict(collections.Counter)
    paired = 0
    for sh, fs in ia.items():
        gs = ib.get(sh)
        if not gs or len(fs) != 1 or len(gs) != 1:
            continue
        paired += 1
        for (_, da), (_, db) in zip(a[fs[0]], b[gs[0]]):
            if len(da) == len(db):
                for x, y in zip(da, db):
                    votes[x][y] += 1
    out = {}
    for x, c in votes.items():
        (y, n), = c.most_common(1)
        if len(c) > 1 and c.most_common(2)[1][1] == n:
            continue
        out[x] = (y, n, sum(c.values()))
    return paired, out


def propagate(a, b, known, rounds=8, minshared=2):
    """Grows the translation. Functions are paired by how many offsets they
    already agree on, and each pair is then aligned instruction by instruction
    so every offset the alignment lines up is a new pair. An offset that comes
    out two different ways is dropped rather than guessed at."""
    known = dict(known)
    for _ in range(rounds):
        seta = {f: {d for _, ds in a[f] for d in ds} for f in a}
        setb = {g: {d for _, ds in b[g] for d in ds} for g in b}
        added = 0
        proposals = collections.defaultdict(collections.Counter)
        for f, sa in seta.items():
            mapped = {known[v] for v in sa if v in known}
            if len(mapped) < minshared:
                continue
            best, cand = 0, []
            for g, sb in setb.items():
                n = len(mapped & sb)
                if n > best:
                    best, cand = n, [g]
                elif n == best and n:
                    cand.append(g)
            if best < minshared or len(cand) != 1:
                continue
            sm = difflib.SequenceMatcher(a=[s for s, _ in a[f]],
                                         b=[s for s, _ in b[cand[0]]],
                                         autojunk=False)
            for i, j, n in sm.get_matching_blocks():
                for k in range(n):
                    da = a[f][i + k][1]
                    db = b[cand[0]][j + k][1]
                    if len(da) == len(db):
                        for x, y in zip(da, db):
                            proposals[x][y] += 1
        for x, c in proposals.items():
            if x in known:
                continue
            (y, n), = c.most_common(1)
            if len(c) > 1 and c.most_common(2)[1][1] == n:
                continue
            known[x] = y
            added += 1
        if not added:
            break
    return known


def main():
    if len(sys.argv) < 3:
        print("usage: nexmap.py A.DLL B.DLL [offset...]", file=sys.stderr)
        return 2
    a, b = load(sys.argv[1]), load(sys.argv[2])
    paired, tr = translate(a, b)
    print("%d functions in A, %d in B, %d paired, %d offsets carried"
          % (len(a), len(b), paired, len(tr)))
    for w in sys.argv[3:]:
        v = int(w, 16) & 0xFFFF
        if v in tr:
            y, n, tot = tr[v]
            print("  %04x -> %04x  (%d of %d)" % (v, y, n, tot))
        else:
            print("  %04x -> not carried" % v)
    return 0


if __name__ == "__main__":
    sys.exit(main())
