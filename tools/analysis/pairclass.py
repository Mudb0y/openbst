#!/usr/bin/env python3
"""Reads out which events each sound emits in a build's pair scan.

The scan is one switch over the sound code, and every build compiles a
different one: the same sound is not the same number twice, and a language
without aspirated stops has no case for them. Rather than guess the cases
from the phoneme attributes, this follows the compiled switch -- a chain of
compares in some builds, a jump table in others -- and classifies each arm by
the calls it makes.

The classes are the ones bst_pairs knows: 1 is the segment then one
transition, 2 is a transition either side, 3 is a transition then the segment,
4 is the segment alone, 5 is the segment then the second transition, 6 is a
vowel's four events and 7 is a consonant's.
"""

import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import capstone
import immdiff
import xmap

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True

ENG_SCAN, ENG_TRANS, ENG_PAIR, ENG_VDUR = 0x10007410, 0x10007E20, 0x100079E0, 0x10007A90

CLASS = {
    ("p", "t0"): 1,
    ("t0", "p", "t1"): 2,
    ("t0", "p"): 3,
    ("p",): 4,
    ("p", "t1"): 5,
    ("t0", "p", "tv", "t2"): 6,
    ("t0", "p", "t1", "t2"): 7,
    ("p", "t1", "t2"): 8,
}


def image(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + oh + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        secs.append((base + va, max(vs, rs), ro, rs))
    def read(va, n):
        for sva, sz, ro, rs in secs:
            if sva <= va < sva + sz and va - sva < rs:
                return d[ro + (va - sva):ro + (va - sva) + n]
        return b""
    return read


def body(path, lo, n):
    t, _ = xmap.load(path)
    va, code = t
    return list(MD.disasm(code[lo - va:lo - va + n], lo))


def arms(path, fn, end):
    """code -> the address the switch jumps to, and the highest code."""
    ins = body(path, fn, end - fn)
    at = {i.address: k for k, i in enumerate(ins)}
    read = image(path)
    hi, out = 0, {}
    R8 = ("al, ", "bl, ", "cl, ", "dl, ")
    for k, i in enumerate(ins):
        # The bound check that opens the switch.
        if i.mnemonic == "cmp" and i.op_str.startswith(R8) and \
           k + 1 < len(ins) and ins[k + 1].mnemonic == "ja":
            hi = int(i.op_str.split(", ")[1], 0)
            break
    for k, i in enumerate(ins):
        if i.mnemonic == "cmp" and i.op_str.startswith(R8):
            v = int(i.op_str.split(", ")[1], 0)
            if v < 1 or v > hi:
                continue
            # The branch does not always follow the compare: the compiler
            # slips unrelated moves in between.
            j = k + 1
            while j < len(ins) and ins[j].mnemonic in ("mov", "movzx", "lea"):
                j += 1
            if j >= len(ins) or ins[j].mnemonic not in ("je", "jne"):
                continue
            if ins[j].mnemonic == "je":
                out[v] = ins[j].operands[0].imm
            else:
                out[v] = ins[j + 1].address
        if i.mnemonic == "jmp" and "*4" in i.op_str and "ptr" in i.op_str:
            jt = int(i.op_str.rsplit("+ ", 1)[1].rstrip("]"), 0)
            idx = int(ins[k - 1].op_str.rsplit("+ ", 1)[1].rstrip("]"), 0)
            tbl = read(idx, hi)
            n = max(tbl) + 1
            targets = struct.unpack("<%dI" % n, read(jt, 4 * n))
            for c in range(1, hi + 1):
                out[c] = targets[tbl[c - 1]]
            break
    return out, hi


def walk(path, at, trans, pair, vdur, stop):
    """The events one arm emits, in order."""
    seen, out, pushes = set(), [], []
    for _ in range(200):
        if at in seen or at >= stop:
            break
        seen.add(at)
        ins = body(path, at, 64)
        for i in ins:
            if i.address >= stop:
                return out
            if i.mnemonic == "push":
                pushes.append(i.op_str)
            elif i.mnemonic == "call":
                t = i.operands[0].imm
                if t == pair:
                    out.append("p")
                    pushes = []
                elif t == vdur:
                    pushes = ["vdur"]
                elif t == trans:
                    which = pushes[-3] if len(pushes) >= 3 else "?"
                    if "vdur" in pushes:
                        out.append("tv")
                    elif which in ("1",):
                        out.append("t1")
                    elif which in ("2",):
                        out.append("t2")
                    else:
                        out.append("t0")
                    pushes = []
                else:
                    pushes = []
            elif i.mnemonic == "jmp":
                if i.operands[0].type != capstone.x86.X86_OP_IMM:
                    return out
                at = i.operands[0].imm
                break
            elif i.mnemonic.startswith("j") or i.mnemonic == "ret":
                return out
        else:
            return out
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: pairclass.py ENGLISH_DLL DLL...", file=sys.stderr)
        return 2
    ta, ra = xmap.load(sys.argv[1])
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    starts = sorted(xmap.call_targets(ta))
    for path in sys.argv[2:]:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper().replace("DLL_", "")
        if path == sys.argv[1]:
            g, trans, pair, vdur = ENG_SCAN, ENG_TRANS, ENG_PAIR, ENG_VDUR
            fb, sb = fa, starts
        else:
            tb, rb = xmap.load(path)
            fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
            sig = xmap.match_signatures(fa, fb)
            known = xmap.propagate(fa, fb, xmap.pairs_from_matches(fa, fb, sig))
            pairs = immdiff.fn_pairs(fa, fb, sig, known)
            g = pairs.get(ENG_SCAN)
            trans = known.get(ENG_TRANS) or pairs.get(ENG_TRANS)
            pair = known.get(ENG_PAIR) or pairs.get(ENG_PAIR)
            vdur = known.get(ENG_VDUR) or pairs.get(ENG_VDUR)
            sb = sorted(xmap.call_targets(tb))
        if not g or not trans or not pair:
            print("/* %s: pair scan not found */" % name)
            continue
        stop = min([x for x in sb if x > g] + [g + 0x4000])
        tgt, hi = arms(path, g, stop)
        cls = [0] * 0x40
        bad = []
        for c in range(1, min(hi, 0x3F) + 1):
            if c not in tgt:
                continue
            ev = tuple(walk(path, tgt[c], trans, pair, vdur, stop))
            k = CLASS.get(ev)
            if k is None:
                bad.append((c, ev))
            else:
                cls[c] = k
        print("    /* %s */" % name)
        print("    .pair_class = {")
        for i in range(0, 0x40, 16):
            print("        " + " ".join("%d," % x for x in cls[i:i + 16]))
        print("    },")
        if bad:
            print("/* %s unclassified: %s */" % (name, bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
