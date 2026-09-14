#!/usr/bin/env python3
"""Reads the input character map a build applies before it tokenises.

Say_TTS hands the text to a pass that rewrites a few characters -- a build
that treats a plain letter as its accented form does it here -- and then puts
every character through a switch over the Latin-1 range. Decoding both gives
a 256-byte table the library can apply as it reads.
"""

import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import capstone
import immdiff
import xmap

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True

def export(path, want):
    """The address a DLL exports under a name."""
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs = []
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    for i in range(nsec):
        o = pe + 24 + oh + i * 40
        vs, rva, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        secs.append((rva, max(vs, rs), ro, rs))
    def off(rva):
        for sva, sz, ro, rs in secs:
            if sva <= rva < sva + sz and rva - sva < rs:
                return ro + (rva - sva)
        return None
    ed = struct.unpack_from("<I", d, pe + 24 + 96)[0]
    e = off(ed)
    n, af, an, ao = struct.unpack_from("<IIII", d, e + 24)
    for i in range(n):
        nm = struct.unpack_from("<I", d, off(an) + i * 4)[0]
        s = d[off(nm):].split(b"\0")[0].decode()
        if s == want:
            ord_ = struct.unpack_from("<H", d, off(ao) + i * 2)[0]
            return base + struct.unpack_from("<I", d, off(af) + ord_ * 4)[0]
    return None


def body(path, lo, n):
    t, _ = xmap.load(path)
    va, code = t
    return list(MD.disasm(code[lo - va:lo - va + n], lo))


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


def switch_map(path, fn):
    """The Latin-1 switch, as {code: value}."""
    ins = body(path, fn, 0x80)
    lo = jt = idx = None
    for k, i in enumerate(ins):
        if i.mnemonic == "add" and i.op_str.startswith("eax, 0x"):
            lo = (-(0x100000000 - int(i.op_str.split(", ")[1], 0))) & 0xFF
            lo = (0x100 - lo) & 0xFF
            lo = (0 - int(i.op_str.split(", ")[1], 0)) & 0xFF
        if i.mnemonic == "cmp" and i.op_str.startswith("eax, 0x"):
            span = int(i.op_str.split(", ")[1], 0)
        if i.mnemonic == "mov" and "byte ptr [eax + 0x" in i.op_str:
            idx = int(i.op_str.split("+ ")[1].rstrip("]"), 0)
        if i.mnemonic == "jmp" and "*4 + 0x" in i.op_str:
            jt = int(i.op_str.rsplit("+ ", 1)[1].rstrip("]"), 0)
            break
    if lo is None or jt is None or idx is None:
        return {}
    tbl = read(path, idx, span + 1)
    n = max(tbl) + 1
    targets = struct.unpack("<%dI" % n, read(path, jt, 4 * n))
    out = {}
    for c in range(span + 1):
        arm = body(path, targets[tbl[c]], 8)
        if arm and arm[0].mnemonic == "mov" and arm[0].op_str.startswith("al, 0x"):
            out[(lo + c) & 0xFF] = int(arm[0].op_str.split(", ")[1], 0)
    return out


def prepass(path, fn, end):
    """The rewrites before the switch, and the switch's own function."""
    ins = body(path, fn, min(end - fn, 0x200))
    subs, call = {}, None
    for k, i in enumerate(ins):
        if i.mnemonic == "cmp" and "byte ptr [" in i.op_str and ", 0x" in i.op_str:
            src = i.op_str.rsplit(", ", 1)[0]
            v = int(i.op_str.rsplit(", ", 1)[1], 0)
            for j in ins[k + 1:k + 4]:
                if j.mnemonic == "mov" and j.op_str.startswith(src + ", 0x"):
                    subs[v] = int(j.op_str.rsplit(", ", 1)[1], 0)
                    break
        if i.mnemonic == "call" and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm
            a = body(path, t, 0x20)
            if len(a) > 3 and a[1].mnemonic == "and" and a[1].op_str == "eax, 0xff":
                call = t
    return subs, call


def main():
    if len(sys.argv) < 3:
        print("usage: inmap.py ENGLISH_DLL DLL...", file=sys.stderr)
        return 2
    ta, ra = xmap.load(sys.argv[1])
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    for path in sys.argv[2:]:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper().replace("DLL_", "")
        tb, rb = xmap.load(path)
        fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
        sig = xmap.match_signatures(fa, fb)
        known = xmap.propagate(fa, fb, xmap.pairs_from_matches(fa, fb, sig))
        say = export(path, "Say_TTS")
        starts = sorted(xmap.call_targets(tb))
        g, best = None, 0
        if say:
            for i in body(path, say, 0x100):
                if i.mnemonic == "call" and \
                   i.operands[0].type == capstone.x86.X86_OP_IMM:
                    t = i.operands[0].imm
                    nxt = min([x for x in starts if x > t] + [t + 0x1000])
                    if nxt - t > best:
                        g, best = t, nxt - t
                if i.mnemonic == "ret":
                    break
        if not g:
            print("/* %s: speak entry not found */" % name)
            continue
        end = min([x for x in starts if x > g] + [g + 0x400])
        subs, call = prepass(path, g, end)
        table = list(range(256))
        if call:
            for c, v in switch_map(path, call).items():
                table[c] = v
        for c, v in subs.items():
            table[c] = table[v] if v < 256 else v
        if table == list(range(256)):
            print("/* %s: identity */" % name)
            continue
        print("    /* %s */" % name)
        print("    .in_map = {")
        for i in range(0, 256, 16):
            print("        " + " ".join("0x%02X," % x for x in table[i:i + 16]))
        print("    },")
    return 0


if __name__ == "__main__":
    sys.exit(main())
