#!/usr/bin/env python3
"""Finds the 1995 build's tables in another build.

Signature matching alone is not enough: a table whose first bytes differ still
matches from its third byte on, and reporting that as the table's address is
wrong by two. So every sixteen-byte window of the reference table votes for a
base address, and the base with the most votes wins. The score that comes back
is how many of the table's bytes agree at that base, which is what says whether
the table is the same table or merely a near neighbour.

Addresses come back in whatever the target's own addressing is: a virtual
address for a PE build, segment and offset packed as (segment << 16) | offset
for a 16-bit NE one.
"""

import collections
import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800

# Each entry is a name, its 1995 virtual address and its length.
TABLES = [
    ("coefgain",     0x10020000, 0x208),
    ("log",          0x10020208, 512),
    ("alog",         0x10020408, 512),
    ("noise",        0x10020608, 64),
    ("pulse",        0x10020648, 320),
    ("suffix_ptrs",  0x10020E90, 16),
    ("code_medial",  0x10020EA0, 112),
    ("code_initial", 0x10020F10, 112),
    ("chattr",       0x10021020, 256),
    ("letterattr",   0x10021120, 256),
    ("casemap",      0x10021220, 256),
    ("symmap",       0x10021320, 256),
    ("basedur",      0x10021420, 32),
    ("phattr1",      0x10021648, 128),
    ("phattr2",      0x100216C8, 128),
    ("voices",       0x10022284, 410),
    ("trans_pitch",  0x10022B58, 592),
    ("vowel_dur",    0x10022DA8, 288),
    ("diph_records", 0x100292A0, 4096),
    ("diph_offsets", 0x1002D508, 4880),
    ("patterns",     0x1002EFB8, 1024),
    ("outputs",      0x10021C80, 1024),
]


def sections_pe(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 2] != b"PE":
        return None
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opthdr = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        h = pe + 24 + opthdr + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", d, h + 8)
        out.append((base + va, max(vs, rs), ro, rs))
    return out


def sections_ne(d):
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    if d[ne:ne + 2] != b"NE":
        return None
    nseg = struct.unpack_from("<H", d, ne + 0x1C)[0]
    seg_off = struct.unpack_from("<H", d, ne + 0x22)[0]
    shift = struct.unpack_from("<H", d, ne + 0x32)[0] or 9
    out = []
    for i in range(nseg):
        sector, length, flags, alloc = struct.unpack_from(
            "<HHHH", d, ne + seg_off + i * 8)
        out.append((((i + 1) << 16), length or 0x10000,
                    sector << shift, length or 0x10000))
    return out


def to_addr(secs, off):
    for va, vsize, raw, rawsize in secs:
        if raw and raw <= off < raw + rawsize:
            return va + (off - raw)
    return None


def locate(ref, target, start, length, window=16, step=4, minvotes=3):
    votes = collections.Counter()
    for r in range(0, max(1, length - window + 1), step):
        pat = ref[start + r:start + r + window]
        if len(pat) < window or len(set(pat)) < 3:
            continue          # too bland to identify anything
        at = target.find(pat)
        while at >= 0:
            votes[at - r] += 1
            at = target.find(pat, at + 1)
    best, score, count = None, 0, 0
    for base, v in votes.most_common(24):
        if v < minvotes or base < 0 or base + length > len(target):
            continue
        agree = sum(1 for i in range(length)
                    if ref[start + i] == target[base + i])
        if agree > score:
            best, score, count = base, agree, v
    return best, score


def main():
    if len(sys.argv) < 3:
        print("usage: locate.py REFERENCE TARGET...", file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    for path in sys.argv[2:]:
        d = open(path, "rb").read()
        secs = sections_pe(d) or sections_ne(d)
        if not secs:
            print("%s: neither PE nor NE" % path)
            continue
        print("%s" % path)
        for name, va, length in TABLES:
            start = RDATA_OFF + (va - RDATA_VA)
            base, score = locate(ref, d, start, length)
            if base is None:
                print("    %-14s not found" % name)
                continue
            addr = to_addr(secs, base)
            print("    %-14s file 0x%06x  addr %s  %d/%d bytes agree"
                  % (name, base,
                     "0x%08x" % addr if addr is not None else "unmapped",
                     score, length))
    return 0


if __name__ == "__main__":
    sys.exit(main())
