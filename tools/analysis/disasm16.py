#!/usr/bin/env python3
"""Disassembles 16-bit code out of an NE module, by segment and offset.

The 1998 family is Windows 3.x code, so the 32-bit disassembler in disasm.py
reads it as nonsense. This one takes a segment number and an offset within it
and works in the file, which is where the bytes are before the loader moves
them.
"""

import struct
import sys

import capstone


def segments(d):
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    nseg = struct.unpack_from("<H", d, ne + 0x1C)[0]
    seg_off = struct.unpack_from("<H", d, ne + 0x22)[0]
    shift = struct.unpack_from("<H", d, ne + 0x32)[0] or 9
    out = []
    for i in range(nseg):
        sector, length, flags, alloc = struct.unpack_from(
            "<HHHH", d, ne + seg_off + i * 8)
        out.append((sector << shift, length or 0x10000, flags))
    return out


def main():
    args = sys.argv[1:]
    raw = False
    if args and args[0] == "--raw":
        raw = True
        args = args[1:]
    if len(args) < 2:
        print("usage: disasm16.py DLL SEG OFFSET [COUNT]\n"
              "       disasm16.py --raw SEGFILE OFFSET [COUNT]", file=sys.stderr)
        return 2
    d = open(args[0], "rb").read()
    if raw:
        base, length = 0, len(d)
        off = int(args[1], 0)
        count = int(args[2]) if len(args) > 2 else 60
    else:
        seg = int(args[1])
        off = int(args[2], 0)
        count = int(args[3]) if len(args) > 3 else 60
        base, length, flags = segments(d)[seg - 1]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = False
    code = d[base + off:base + length]
    n = 0
    for ins in md.disasm(code, off):
        print("%04x  %-20s %s %s" % (ins.address, ins.bytes.hex(),
                                     ins.mnemonic, ins.op_str))
        n += 1
        if n >= count:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
