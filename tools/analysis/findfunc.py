#!/usr/bin/env python3
"""Finds the same function in another build of the same engine.

The builds are the same source compiled for different languages, so a routine
that is not language-specific has the same instruction bytes in all of them
apart from the absolute addresses it names. Masking out anything that looks
like an address in the reference and searching for the rest locates it.
"""

import struct
import sys


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(n):
        h = pe + 24 + optsz + i * 40
        vsize, va, rawsz, rawptr = struct.unpack_from("<IIII", d, h + 8)
        out.append((va, vsize, rawptr, rawsz))
    return base, out


def va2off(base, secs, va):
    rva = va - base
    for sva, vsize, rawptr, rawsz in secs:
        if sva <= rva < sva + max(vsize, rawsz):
            return rawptr + (rva - sva)
    return None


def off2va(base, secs, off):
    for sva, vsize, rawptr, rawsz in secs:
        if rawptr <= off < rawptr + rawsz:
            return base + sva + (off - rawptr)
    return None


def mask(pat, lo, hi):
    """Positions whose four bytes read as an address in the reference are
    wildcards, because the other build puts its tables elsewhere."""
    holes = set()
    for i in range(len(pat) - 3):
        v = struct.unpack_from("<I", pat, i)[0]
        if lo <= v < hi:
            holes.update(range(i, i + 4))
    return holes


def main():
    if len(sys.argv) < 4:
        print("usage: findfunc.py REFERENCE_DLL VA LENGTH TARGET_DLL...",
              file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    va = int(sys.argv[2], 0)
    n = int(sys.argv[3], 0)
    rbase, rsecs = sections(ref)
    o = va2off(rbase, rsecs, va)
    if o is None:
        print("address outside every section", file=sys.stderr)
        return 1
    pat = ref[o:o + n]
    holes = mask(pat, rbase, rbase + 0x00200000)

    for path in sys.argv[4:]:
        d = open(path, "rb").read()
        base, secs = sections(d)
        text = None
        for sva, vsize, rawptr, rawsz in secs:
            if rawptr <= o < rawptr + rawsz or text is None:
                text = (rawptr, rawsz)
                break
        found = None
        start, size = text
        for i in range(start, start + size - n):
            ok = True
            for k in range(n):
                if k in holes:
                    continue
                if d[i + k] != pat[k]:
                    ok = False
                    break
            if ok:
                found = off2va(base, secs, i)
                break
        print("%-24s %s" % (path.rsplit("/", 1)[-1],
                            "0x%08x" % found if found else "not found"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
