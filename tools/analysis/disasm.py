#!/usr/bin/env python3
"""Disassembles a range of a PE image by virtual address.

Ghidra's headless output covers the 1995 build; this is for the 2006 builds
and for spot checks, where loading a project costs more than the answer is
worth.
"""

import struct
import sys

import capstone


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rawsz, rawptr = struct.unpack_from("<IIII", d, o + 8)
        out.append((name, va, vsize, rawptr, rawsz))
    return base, out


def va2off(base, secs, va):
    rva = va - base
    for name, sva, vsize, rawptr, rawsz in secs:
        if sva <= rva < sva + max(vsize, rawsz):
            return rawptr + (rva - sva)
    return None


def main():
    if len(sys.argv) < 3:
        print("usage: disasm.py DLL START [LENGTH]", file=sys.stderr)
        return 2
    d = open(sys.argv[1], "rb").read()
    base, secs = sections(d)
    start = int(sys.argv[2], 0)
    length = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x80
    off = va2off(base, secs, start)
    if off is None:
        print("address outside every section", file=sys.stderr)
        return 1
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(d[off:off + length], start):
        print("%08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
