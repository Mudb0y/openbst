#!/usr/bin/env python3
"""Names the tables a build actually reads, from a read trace.

The oracle records the address and the program counter of every read into the
image. The program counter is the useful half: the instruction that made the
read carries the table's base as its displacement, so the base comes out exact
rather than as the lowest index the sentence happened to use. The order in
which the instructions first run is an algorithmic fact and the same in every
build, which is what lets two builds' tables be lined up against each other.
"""

import collections
import struct
import sys

import capstone


def load_text(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
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
        if ro:
            if lo is None or base + va < lo:
                lo = base + va
            hi = max(hi, base + va + size)
    return text, (lo, hi)


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def displacement(text, pc, drange):
    """The image address the instruction at pc names, if any. Disassembling
    from the instruction itself would need its start, which the trace does not
    give, so a short run-up is tried until one lands exactly on it."""
    va, t = text
    lo, hi = drange
    for back in range(0, 16):
        start = pc - back
        if start < va:
            continue
        for ins in MD.disasm(t[start - va:start - va + 24], start):
            if ins.address != pc:
                break
            for op in ins.operands:
                v = None
                if op.type == capstone.x86.X86_OP_MEM:
                    v = op.mem.disp & 0xFFFFFFFF
                elif op.type == capstone.x86.X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                if v is not None and lo <= v < hi:
                    return v, ins.mnemonic, ins.op_str
            return None
    return None


def image_immediates(text, drange):
    """Every image address the code names anywhere. A table reached through a
    register is loaded by an instruction whose immediate is the table's base,
    so the base of a read is the largest of these that the read did not go
    below."""
    va, t = text
    lo, hi = drange
    out = set()
    for i in range(len(t) - 5):
        if 0xB8 <= t[i] <= 0xBF:
            v = struct.unpack_from("<I", t, i + 1)[0]
            if lo <= v < hi:
                out.add(v)
        elif t[i] == 0x68:
            v = struct.unpack_from("<I", t, i + 1)[0]
            if lo <= v < hi:
                out.add(v)
    for ins in MD.disasm(t, va):
        for op in ins.operands:
            v = None
            if op.type == capstone.x86.X86_OP_MEM:
                v = op.mem.disp & 0xFFFFFFFF
            elif op.type == capstone.x86.X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
            if v is not None and lo <= v < hi:
                out.add(v)
    return sorted(out)


def base_below(imms, addr):
    import bisect
    i = bisect.bisect_right(imms, addr) - 1
    return imms[i] if i >= 0 else None


def read_trace(path):
    """First appearance of each program counter, in order, with how many reads
    it made and the address range it covered."""
    d = open(path, "rb").read()
    order = []
    seen = {}
    n = len(d) // 12
    for i in range(n):
        a, p = struct.unpack_from("<II", d, i * 12)
        e = seen.get(p)
        if e is None:
            seen[p] = [len(order), 1, a, a]
            order.append(p)
        else:
            e[1] += 1
            if a < e[2]:
                e[2] = a
            if a > e[3]:
                e[3] = a
    return order, seen


def main():
    if len(sys.argv) < 3:
        print("usage: tabtrace.py DLL TRACE [minreads]", file=sys.stderr)
        return 2
    text, drange = load_text(sys.argv[1])
    minreads = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    order, seen = read_trace(sys.argv[2])
    imms = image_immediates(text, drange)
    tables = collections.OrderedDict()
    for p in order:
        idx, count, lo, hi = seen[p]
        if count < minreads:
            continue
        got = displacement(text, p, drange)
        if got:
            base, mn, ops = got
            how = "named"
        else:
            base = base_below(imms, lo)
            if base is None:
                continue
            mn, ops, how = "", "via a register", "inferred"
        if base not in tables:
            tables[base] = [idx, 0, mn, ops, lo, hi, how]
        e = tables[base]
        e[1] += count
        e[4] = min(e[4], lo)
        e[5] = max(e[5], hi)
    for base in sorted(tables, key=lambda b: tables[b][0]):
        idx, count, mn, ops, lo, hi, how = tables[base]
        print("%08x  %-8s order %-7d %8d reads  span %08x..%08x  %s %s"
              % (base, how, idx, count, lo, hi, mn, ops))
    return 0


if __name__ == "__main__":
    sys.exit(main())
