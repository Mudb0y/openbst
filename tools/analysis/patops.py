#!/usr/bin/env python3
"""Reads out what each character in a rule pattern tests.

The context matcher dispatches on the pattern character. Some builds spell
every operator as a letter-attribute bit and take one character; the English
ones have runs, suffixes and a vowel test besides. This decodes the dispatch
and, where every arm is a plain bit test, prints the table the library needs.
"""

import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import capstone
import xmap

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def read(path, va, n):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    for i in range(nsec):
        o = pe + 24 + oh + i * 40
        vs, rva, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        if base + rva <= va < base + rva + max(vs, rs) and va - base - rva < rs:
            f = ro + (va - base - rva)
            return d[f:f + n]
    return b""


def main():
    if len(sys.argv) < 2:
        print("usage: patops.py DLL...", file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper().replace("DLL_", "")
        t, r = xmap.load(path)
        va, code = t
        starts = sorted(xmap.call_targets(t))
        ins = []
        for k, f in enumerate(starts):
            stop = starts[k + 1] if k + 1 < len(starts) else va + len(code)
            if stop - f > 0x4000:
                stop = f + 0x4000
            ins.extend(MD.disasm(code[f - va:stop - va], f))
        at = {i.address: k for k, i in enumerate(ins)}
        found = None
        for k, i in enumerate(ins):
            if i.mnemonic != "add" or not i.op_str.endswith(", -0x21"):
                continue
            j = k + 1
            if j + 3 >= len(ins):
                continue
            if ins[j].mnemonic != "cmp" or not ins[j].op_str.endswith(", 0x3d"):
                continue
            for m in ins[j:j + 6]:
                if m.mnemonic == "jmp" and "*4 + 0x" in m.op_str:
                    jt = int(m.op_str.rsplit("+ ", 1)[1].rstrip("]"), 0)
                    idx = int(ins[at[m.address] - 1].op_str.rsplit("+ ", 1)[1]
                              .rstrip("]"), 0)
                    found = (jt, idx)
                    break
            if found:
                break
        if not found:
            print("/* %s: no bit dispatch */" % name)
            continue
        jt, idx = found
        tbl = read(path, idx, 0x3E)
        n = max(tbl) + 1
        targets = struct.unpack("<%dI" % n, read(path, jt, 4 * n))
        bits = {}
        for c in range(0x21, 0x21 + 0x3E):
            arm = list(MD.disasm(read(path, targets[tbl[c - 0x21]], 0x20),
                                 targets[tbl[c - 0x21]]))
            for a in arm[:4]:
                if a.mnemonic == "test" and "byte ptr [" in a.op_str and \
                   ", 0x" in a.op_str:
                    bits[c] = int(a.op_str.rsplit(", ", 1)[1], 0)
                    break
        if not bits:
            print("/* %s: dispatch found, no bit arms */" % name)
            continue
        print("    /* %s */" % name)
        print("    .pat_bit = {")
        for c in sorted(bits):
            print("        [0x%02X] = 0x%02X," % (c, bits[c]))
        print("    },")
    return 0


if __name__ == "__main__":
    sys.exit(main())
