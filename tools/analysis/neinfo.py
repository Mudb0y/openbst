#!/usr/bin/env python3
"""Reports the shape of a 16-bit NE module: segments, relocations, imports and
the exported entry table.

The 1998 language family is Windows 3.x modules, which the oracle could not
load; this says what loading them involves and which host ordinals a shim layer
has to answer.
"""

import struct
import sys

ADDRTYPE = {0: "lobyte", 2: "segment", 3: "far32", 5: "offset16",
            11: "far48", 13: "offset32"}
RELTYPE = {0: "internal", 1: "ord", 2: "name", 3: "osfixup"}


def report(path, verbose):
    d = open(path, "rb").read()
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    if d[ne:ne + 2] != b"NE":
        print("%s: not an NE module" % path)
        return

    def u16(o):
        return struct.unpack_from("<H", d, ne + o)[0]

    def u32(o):
        return struct.unpack_from("<I", d, ne + o)[0]

    flags, ds, heap, stack = u16(0x0C), u16(0x0E), u16(0x10), u16(0x12)
    csip = u32(0x14)
    nseg, nmod = u16(0x1C), u16(0x1E)
    ent_off, ent_len = u16(0x04), u16(0x06)
    seg_off, res_off, mod_off, imp_off = u16(0x22), u16(0x26), u16(0x28), u16(0x2A)
    nonres_off, nonres_len = u32(0x2C), u16(0x30)
    shift = u16(0x32) or 9

    print("%s" % path.rsplit("/", 1)[-1])
    print("  flags %04x  segments %d  modules %d  align 1<<%d"
          % (flags, nseg, nmod, shift))
    print("  entry cs:ip %04x:%04x  ds %d  stack %d heap %d"
          % (csip >> 16, csip & 0xFFFF, ds, stack, heap))

    def impname(noff):
        p = ne + imp_off + noff
        return d[p + 1:p + 1 + d[p]].decode("latin1")

    mods = [impname(struct.unpack_from("<H", d, ne + mod_off + i * 2)[0])
            for i in range(nmod)]

    total = 0
    wanted = {}
    for i in range(nseg):
        o = ne + seg_off + i * 8
        sector, length, sflags, minalloc = struct.unpack_from("<HHHH", d, o)
        if length == 0:
            length = 0x10000
        if minalloc == 0:
            minalloc = 0x10000
        total += minalloc
        base = sector << shift
        nrel = 0
        rels = []
        if sflags & 0x100 and sector:
            p = base + length
            nrel = struct.unpack_from("<H", d, p)[0]
            for k in range(nrel):
                rels.append(struct.unpack_from("<BBHHH", d, p + 2 + k * 8))
        print("    seg %2d  file %08x  len %5d  alloc %5d  flags %04x  %s  %d relocs"
              % (i + 1, base, length, minalloc, sflags,
                 "data" if sflags & 1 else "code", nrel))
        for at, rt, off, a, b in rels:
            key = None
            if (rt & 3) == 1:
                key = (mods[a - 1] if 0 < a <= nmod else "mod%d" % a, b)
            elif (rt & 3) == 2:
                key = (mods[a - 1] if 0 < a <= nmod else "mod%d" % a, impname(b))
            if key:
                wanted[key] = wanted.get(key, 0) + 1
            if verbose:
                print("        %-8s %-8s at %04x  %04x %04x"
                      % (ADDRTYPE.get(at & 0x0F, "at%d" % at),
                         RELTYPE.get(rt & 3, "?"), off, a, b))
    print("    total %d bytes allocated" % total)
    print("  imports from: %s" % (", ".join(mods) if mods else "nothing"))
    if wanted:
        print("  host entries referenced:")
        for (m, w) in sorted(wanted, key=lambda k: (k[0], str(k[1]))):
            print("    %-10s %-24s x%d" % (m, w, wanted[(m, w)]))

    names = []
    p = ne + res_off
    while d[p]:
        n = d[p]
        names.append((d[p + 1:p + 1 + n].decode("latin1"),
                      struct.unpack_from("<H", d, p + 1 + n)[0]))
        p += 1 + n + 2
    if names:
        print("  module name %s, resident exports: %s"
              % (names[0][0],
                 ", ".join("%s@%d" % t for t in names[1:]) or "none"))
    nonres = []
    if nonres_len:
        p = nonres_off
        while p < nonres_off + nonres_len and d[p]:
            n = d[p]
            nonres.append((d[p + 1:p + 1 + n].decode("latin1"),
                           struct.unpack_from("<H", d, p + 1 + n)[0]))
            p += 1 + n + 2
    if nonres:
        print("  nonresident: %s" % ", ".join("%s@%d" % t for t in nonres))

    ents = {}
    p, ordinal = ne + ent_off, 1
    while p < ne + ent_off + ent_len:
        cnt, ind = d[p], d[p + 1]
        p += 2
        if cnt == 0:
            break
        if ind == 0:
            ordinal += cnt
            continue
        for _ in range(cnt):
            if ind == 0xFF:
                ents[ordinal] = (d[p + 3], struct.unpack_from("<H", d, p + 4)[0])
                p += 6
            else:
                ents[ordinal] = (ind, struct.unpack_from("<H", d, p + 1)[0])
                p += 3
            ordinal += 1
    if ents:
        print("  entry table: %d ordinals" % len(ents))
        byname = {o: n for n, o in names[1:] + nonres}
        for o in sorted(ents):
            s, off = ents[o]
            print("    @%-4d seg %2d:%04x  %s" % (o, s, off, byname.get(o, "")))


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv[1:]
    if not args:
        print("usage: neinfo.py [-v] DLL...", file=sys.stderr)
        return 2
    for path in args:
        report(path, verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
