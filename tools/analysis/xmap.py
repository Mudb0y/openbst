#!/usr/bin/env python3
"""Carries a table map from one build to another through the code.

Two builds of the same engine reference the same tables from the same places,
so a function that reads four tables in one build reads the same four in the
other. Starting from the addresses locate.py has already placed, functions can
be paired by how many known tables they share, and a pair of functions then
says what the tables neither build has placed yet must be. Repeating that
spreads the map outwards until nothing new is learned.

Reading the code rather than the data is what places the tables that hold
pointers, which have no byte pattern in common between builds because every
pointer in them has moved.
"""

import collections
import difflib
import struct
import sys

import capstone


def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 2] != b"PE":
        raise SystemExit("%s: not a PE build" % path)
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    text = None
    lo, hi = None, 0
    for i in range(nsec):
        h = pe + 24 + oh + i * 40
        flags = struct.unpack_from("<I", d, h + 36)[0]
        vs, va, rs, ro = struct.unpack_from("<IIII", d, h + 8)
        size = max(vs, rs)
        if flags & 0x20000000:
            text = (base + va, d[ro:ro + rs])
        if ro or (flags & 0x20000000):
            if lo is None or base + va < lo:
                lo = base + va
            hi = max(hi, base + va + size)
    # Code addresses count as references too: a table of handler addresses is
    # as good a fingerprint as a table of data ones, and better, because the
    # transition table has already placed forty-nine of them.
    return text, (lo, hi)


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def call_targets(text):
    """Function starts, found by scanning for the call opcode rather than by
    disassembling the section end to end: a linear pass desynchronises on the
    first island of data and then misses most of the calls after it."""
    va, t = text
    out = set()
    for i in range(len(t) - 5):
        if t[i] != 0xE8:
            continue
        rel = struct.unpack_from("<i", t, i + 1)[0]
        tgt = va + i + 5 + rel
        if va <= tgt < va + len(t):
            out.add(tgt)
    return out


def disasm_fns(text, drange, starts):
    """Each function as a list of instructions, and the same list reduced to a
    signature: the mnemonic and, for every operand, its shape, with any address
    in the image replaced by a placeholder. Two builds of one source compile to
    the same signature even though every address in them differs."""
    va, t = text
    lo, hi = drange
    order = sorted(starts)
    fns = {}
    for k, f in enumerate(order):
        stop = order[k + 1] if k + 1 < len(order) else va + len(t)
        if stop - f > 0x4000:
            stop = f + 0x4000
        body, sig = [], []
        for ins in MD.disasm(t[f - va:stop - va], f):
            shape = [ins.mnemonic]
            addrs = []
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_REG:
                    shape.append("r%d" % op.reg)
                elif op.type == capstone.x86.X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                    if lo <= v < hi:
                        shape.append("A")
                        addrs.append(v)
                    else:
                        shape.append("i%d" % op.imm)
                else:
                    v = op.mem.disp & 0xFFFFFFFF
                    if lo <= v < hi:
                        shape.append("m%d:%d:A" % (op.mem.base, op.mem.index))
                        addrs.append(v)
                    else:
                        shape.append("m%d:%d:%d" % (op.mem.base, op.mem.index,
                                                    op.mem.disp))
            body.append((ins.address, addrs))
            sig.append(" ".join(shape))
        if body:
            fns[f] = (sig, body)
    return fns


def align_pair(a, b):
    """Addresses named at instructions the two functions have in common."""
    siga, bodya = a
    sigb, bodyb = b
    sm = difflib.SequenceMatcher(a=siga, b=sigb, autojunk=False)
    out = []
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            xa = bodya[i + k][1]
            xb = bodyb[j + k][1]
            if len(xa) == len(xb):
                out.extend(zip(xa, xb))
    return out


def match_signatures(fa, fb):
    """Pairs functions whose signature is identical and unique on both sides.
    Two builds of the same source out of the same compiler agree exactly, so
    this needs no seed at all; between generations it finds only the functions
    the compilers happened to agree on, and the seeded propagation does the
    rest."""
    ia = collections.defaultdict(list)
    ib = collections.defaultdict(list)
    for f, (sig, _) in fa.items():
        ia[tuple(sig)].append(f)
    for g, (sig, _) in fb.items():
        ib[tuple(sig)].append(g)
    out = {}
    for sig, fs in ia.items():
        gs = ib.get(sig)
        if gs and len(fs) == 1 and len(gs) == 1 and len(sig) >= 6:
            out[fs[0]] = gs[0]
    return out


def pairs_from_matches(fa, fb, matches):
    votes = collections.defaultdict(collections.Counter)
    for f, g in matches.items():
        for x, y in align_pair(fa[f], fb[g]):
            votes[x][y] += 1
    out = {}
    for x, c in votes.items():
        (y, n), = c.most_common(1)
        if len(c) > 1 and c.most_common(2)[1][1] == n:
            continue
        out[x] = y
    return out


def propagate(fa, fb, known, rounds=8, minshared=2):
    """Grows a map of A's addresses to B's. Functions are paired by how many
    known addresses they agree on; a pair is then aligned instruction by
    instruction and every address the alignment lines up is a new pair.

    A pair is only believed when it is the only reading: an address that comes
    out two different ways is dropped rather than guessed at."""
    known = dict(known)
    for _ in range(rounds):
        seta = {f: {v for _, vs in fa[f][1] for v in vs} for f in fa}
        setb = {f: {v for _, vs in fb[f][1] for v in vs} for f in fb}
        scores = collections.defaultdict(dict)
        for f, sa in seta.items():
            mapped = {known[v] for v in sa if v in known}
            if len(mapped) < minshared:
                continue
            for g, sb in setb.items():
                n = len(mapped & sb)
                if n >= minshared:
                    scores[f][g] = n
        proposals = collections.defaultdict(collections.Counter)
        for f, cand in scores.items():
            best = max(cand.values())
            winners = [g for g, n in cand.items() if n == best]
            if len(winners) != 1:
                continue
            for x, y in align_pair(fa[f], fb[winners[0]]):
                proposals[x][y] += 1
        added = 0
        for x, c in proposals.items():
            if x in known:
                continue
            (y, n), = c.most_common(1)
            if len(c) > 1 and c.most_common(2)[1][1] == n:
                continue          # two readings, equally supported
            known[x] = y
            added += 1
        if not added:
            break
    return known

def dedupe(refs):
    seen = set()
    out = []
    for at, v in refs:
        if v in seen:
            continue
        seen.add(v)
        out.append((at, v))
    return out


def main():
    if len(sys.argv) < 4:
        print("usage: xmap.py BUILD_A BUILD_B PAIRS...\n"
              "  each PAIR is addrA:addrB, from locate.py", file=sys.stderr)
        return 2
    ta, ra = load(sys.argv[1])
    tb, rb = load(sys.argv[2])
    fa = disasm_fns(ta, ra, call_targets(ta))
    fb = disasm_fns(tb, rb, call_targets(tb))
    known = {}
    for p in sys.argv[3:]:
        a, b = p.split(":")
        known[int(a, 16)] = int(b, 16)
    print("%d functions in A, %d in B, %d seed pairs"
          % (len(fa), len(fb), len(known)))
    sig = match_signatures(fa, fb)
    if sig:
        direct = pairs_from_matches(fa, fb, sig)
        clash = sum(1 for a, b in direct.items() if a in known and known[a] != b)
        print("%d functions match by signature alone, giving %d pairs, "
              "%d of which contradict a seed" % (len(sig), len(direct), clash))
        for a, b in direct.items():
            known.setdefault(a, b)
    grown = propagate(fa, fb, known)
    print("%d pairs after propagation" % len(grown))
    for a in sorted(grown):
        mark = " seed" if a in known else ""
        print("  %08x -> %08x%s" % (a, grown[a], mark))
    return 0


if __name__ == "__main__":
    sys.exit(main())
