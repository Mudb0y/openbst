#!/usr/bin/env python3
"""Disassembles 16-bit code out of an NE module, by segment and offset.

The 1998 family is Windows 3.x code, so the 32-bit disassembler in disasm.py
reads it as nonsense. This one takes a segment number and an offset within it
and works in the file, which is where the bytes are before the loader moves
them.

--all reads a whole segment rather than a run of instructions, stepping over
any byte the disassembler refuses and carrying on after it. A code segment
with data or padding mixed into it stops capstone dead otherwise, which is
easy to mistake for a short segment: the core module decodes 1587 bytes of
its 47866 before the first such byte and 20678 instructions after this.
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


def whole(md, code, start):
    """Every instruction in code, resuming after each byte that will not
       decode. Returns how many bytes were stepped over."""
    pos, skipped = 0, 0
    while pos < len(code):
        got = 0
        for ins in md.disasm(code[pos:], start + pos):
            print("%04x  %-20s %s %s" % (ins.address, ins.bytes.hex(),
                                         ins.mnemonic, ins.op_str))
            got = ins.address + ins.size - (start + pos)
        if got:
            pos += got
        else:
            pos += 1
            skipped += 1
    return skipped


def main():
    args = sys.argv[1:]
    raw = every = False
    if args and args[0] == "--all":
        every = True
        args = args[1:]
    if args and args[0] == "--raw":
        raw = True
        args = args[1:]
    if len(args) < 2:
        print("usage: disasm16.py [--all] DLL SEG [OFFSET] [COUNT]\n"
              "       disasm16.py [--all] --raw SEGFILE OFFSET [COUNT]",
              file=sys.stderr)
        return 2
    if every and not raw and len(args) < 3:
        args = args + ["0"]
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
    if every:
        skipped = whole(md, code, off)
        print("; %d bytes stepped over" % skipped, file=sys.stderr)
        return 0
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
