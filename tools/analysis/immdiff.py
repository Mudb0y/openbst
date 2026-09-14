#!/usr/bin/env python3
"""Reports the constants that differ between two builds of the engine.

Two builds compile from the same source, so a function's instruction sequence
is the same in both and only the values baked into it change. Pairing the
functions and walking the two instruction streams in step therefore isolates
every per-language constant: the value one build writes into the stream where
the other writes a different one.

Addresses are excluded, because they differ everywhere and say nothing.
"""

import difflib
import sys

import xmap


def imms(sig):
    return [t for t in sig.split() if t.startswith("i")]


def diff_fn(a, b):
    siga, bodya = a
    sigb, bodyb = b
    # Compare on the mnemonic and operand shapes with the immediate values
    # stripped, so an instruction that only changed its constant still lines up.
    ka = [" ".join("i" if t.startswith("i") else t for t in s.split()) for s in siga]
    kb = [" ".join("i" if t.startswith("i") else t for t in s.split()) for s in sigb]
    out = []
    sm = difflib.SequenceMatcher(a=ka, b=kb, autojunk=False)
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            xa, xb = imms(siga[i + k]), imms(sigb[j + k])
            if len(xa) != len(xb) or xa == xb:
                continue
            out.append((bodya[i + k][0], siga[i + k], bodyb[j + k][0], sigb[j + k]))
    return out


def fn_pairs(fa, fb, sig, known):
    """Which function in B is which in A, by how many of the addresses the two
    name are already known to be the same one."""
    seta = {f: {v for _, vs in fa[f][1] for v in vs} for f in fa}
    setb = {g: {v for _, vs in fb[g][1] for v in vs} for g in fb}
    out = dict(sig)
    for f, s in seta.items():
        if f in out:
            continue
        mapped = {known[v] for v in s if v in known}
        if len(mapped) < 3:
            continue
        best, n = None, 0
        for g, t in setb.items():
            k = len(mapped & t)
            if k > n:
                best, n = g, k
            elif k == n:
                best = best if k > 0 else None
        if best is not None and n >= 3:
            out[f] = best
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: immdiff.py BUILD_A BUILD_B [FN_A...]", file=sys.stderr)
        return 2
    ta, ra = xmap.load(sys.argv[1])
    tb, rb = xmap.load(sys.argv[2])
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
    sig = xmap.match_signatures(fa, fb)
    known = xmap.propagate(fa, fb, xmap.pairs_from_matches(fa, fb, sig))
    pair = fn_pairs(fa, fb, sig, known)
    want = [int(x, 16) for x in sys.argv[3:]] or sorted(fa)
    for f in want:
        g = pair.get(f)
        if g is None or g not in fb:
            if len(sys.argv) > 3:
                print("%08x: no pair" % f)
            continue
        d = diff_fn(fa[f], fb[g])
        if not d:
            continue
        print("%08x -> %08x" % (f, g))
        for aa, sa, ab, sb in d:
            print("   %08x %-40s   %08x %s" % (aa, sa, ab, sb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
