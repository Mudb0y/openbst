#!/usr/bin/env python3
"""Turn an oracle read trace into a map of the engine's data tables.

Every read the guest made into the loaded image was logged with the address
and the instruction that issued it. Addresses cluster into tables; the stride
between successive accesses gives the element size; the reading instructions
name the code that owns each table. That is most of the structure of the data
section recovered without reading any disassembly.
"""

import struct
import sys
from collections import Counter, defaultdict

REC = struct.Struct("<IIBxxx")
GAP = 256          # bytes of untouched space that end a table
MIN_READS = 32     # ignore incidental one-off reads


def sections(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    so = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    out = []
    for i in range(nsec):
        o = so + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vs, va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        out.append((name, base + va, max(vs, rs)))
    return base, out


def section_of(secs, addr):
    for name, va, size in secs:
        if va <= addr < va + size:
            return name
    return "?"


def main():
    if len(sys.argv) < 3:
        print("usage: tables.py TRACE.bin DLL [section]", file=sys.stderr)
        return 2
    tracefile, dllfile = sys.argv[1], sys.argv[2]
    want = sys.argv[3] if len(sys.argv) > 3 else ".rdata"

    base, secs = sections(dllfile)
    lo, hi = None, None
    for name, va, size in secs:
        if name == want:
            lo, hi = va, va + size
    if lo is None:
        print("no section %s" % want, file=sys.stderr)
        return 2

    reads = Counter()          # addr -> times read
    pcs = defaultdict(Counter)  # addr -> pc histogram
    widths = Counter()

    data = open(tracefile, "rb").read()
    total = len(data) // REC.size
    for i in range(total):
        addr, pc, size = REC.unpack_from(data, i * REC.size)
        if not (lo <= addr < hi):
            continue
        reads[addr] += 1
        pcs[addr][pc] += 1
        widths[size] += 1

    if not reads:
        print("no reads into %s" % want)
        return 0

    touched = sorted(reads)
    print("trace: %d records, %d into %s, %d distinct addresses (%.1f%% of section)"
          % (total, sum(reads.values()), want, len(touched),
             100.0 * len(touched) / (hi - lo)))
    print("access widths: %s" % ", ".join("%d-byte x%d" % (w, n)
                                          for w, n in sorted(widths.items())))
    print()

    # Split the touched addresses into runs separated by untouched gaps.
    clusters = []
    start = prev = touched[0]
    for a in touched[1:]:
        if a - prev > GAP:
            clusters.append((start, prev))
            start = a
        prev = a
    clusters.append((start, prev))

    print("%-23s %8s %8s %7s  %s" % ("range", "span", "reads", "stride", "read by"))
    for c0, c1 in clusters:
        addrs = [a for a in touched if c0 <= a <= c1]
        nreads = sum(reads[a] for a in addrs)
        if nreads < MIN_READS:
            continue

        # Modal gap between consecutive touched addresses approximates the
        # element size for anything accessed as an array.
        gaps = Counter(b - a for a, b in zip(addrs, addrs[1:]))
        stride = gaps.most_common(1)[0][0] if gaps else 0

        owners = Counter()
        for a in addrs:
            owners.update(pcs[a])
        top = ", ".join("0x%08x" % pc for pc, _ in owners.most_common(3))

        print("0x%08x-0x%08x %8d %8d %7d  %s"
              % (c0, c1, c1 - c0 + 1, nreads, stride, top))

    print()
    print("%d clusters above %d reads" % (
        sum(1 for c0, c1 in clusters
            if sum(reads[a] for a in touched if c0 <= a <= c1) >= MIN_READS),
        MIN_READS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
