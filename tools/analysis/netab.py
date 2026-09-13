#!/usr/bin/env python3
"""Names the tables a 16-bit build reads, and fingerprints them by probe.

The same idea as tabtrace.py and difftrace.py, for the 1998 family. The 16-bit
oracle already reports one line per instruction that read the module's data,
with the address range it covered, so all that is left is to disassemble the
instruction and read the displacement out of it. The segment comes from the
address that was read, the offset from the instruction: together they are the
address in the form the table directory uses.
"""

import collections
import os
import re
import subprocess
import sys
import tempfile

import capstone

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
MD.detail = True

SUITE = [
    ("plain",     "a."),
    ("dict",      "dog."),
    ("rules",     "zzyzx."),
    ("except",    "the."),
    ("stress",    "banana."),
    ("suffix",    "hoped."),
    ("one",       "1."),
    ("teen",      "13."),
    ("ten",       "20."),
    ("oh",        "105."),
    ("hundred",   "300."),
    ("scale",     "1000000."),
    ("zero",      "0."),
    ("group",     "1,234."),
    ("point",     "1.5."),
    ("money",     "$4.50"),
    ("ord_st",    "1st."),
    ("ord_nd",    "2nd."),
    ("ord_rd",    "3rd."),
    ("ord_th",    "4th."),
    ("ord_fifth", "5th."),
    ("ord_tieth", "20th."),
    ("ord_twelfth", "12th."),
    ("charname",  "@."),
    ("dotted",    "A.B.C."),
    ("dash",      "co-op."),
    ("spell",     "12345678901."),
]


def segments(dumpdir, module):
    out = {}
    for name in os.listdir(dumpdir):
        m = re.match(re.escape(module) + r"\.(\d+)\.pre\.bin$", name)
        if m:
            out[int(m.group(1))] = open(os.path.join(dumpdir, name), "rb").read()
    return out


def displacement(segs, pc):
    """The offset the instruction at pc names, if it names one. A short run-up
    is tried until a decode lands exactly on the instruction, because the trace
    gives where it started reading, not where it started."""
    seg, off = pc >> 16, pc & 0xFFFF
    code = segs.get(seg)
    if not code:
        return None
    for back in range(0, 12):
        start = off - back
        if start < 0:
            continue
        for ins in MD.disasm(code[start:start + 20], start):
            if ins.address != off:
                break
            for op in ins.operands:
                # A displacement under a segment's worth of small numbers is a
                # struct field, not a table: [si + 4] says nothing about where
                # anything lives.
                if op.type == capstone.x86.X86_OP_MEM and \
                        (op.mem.disp & 0xFFFF) >= 0x40:
                    return op.mem.disp & 0xFFFF, "%s %s" % (ins.mnemonic, ins.op_str)
            return None
    return None


def run(oracle, lang, text, work):
    trace = os.path.join(work, "t.txt")
    subprocess.run([oracle, "--eof", "-1", "--limit", "400000000",
                    "--lang", lang, "--engine", text + "\n", "--trace", trace],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = []
    if os.path.exists(trace):
        for line in open(trace):
            f = line.split()
            if len(f) == 5:
                out.append(tuple(int(x, 16) for x in f[:3]) +
                           (int(f[3]), int(f[4])))
        os.unlink(trace)
    return out


def named(segs, rows):
    """base -> (how it was read, lowest address, highest, reads, first order)"""
    out = {}
    for pc, lo, hi, n, order in rows:
        got = displacement(segs, pc)
        if got:
            disp, how = got
            base = (lo & 0xFFFF0000) | disp
        else:
            # A table in another segment is reached with the address already in
            # a register, so the instruction names nothing. The lowest address
            # the read reached is then the best that can be said, and it is
            # exact whenever the sentence used the table's first row.
            base = lo
            how = "(no displacement)"
        e = out.get(base)
        if e is None:
            out[base] = [how, lo, hi, n, order]
        else:
            e[1] = min(e[1], lo)
            e[2] = max(e[2], hi)
            e[3] += n
            e[4] = min(e[4], order)
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: netab.py DUMPDIR LANG.DLL [--suite]", file=sys.stderr)
        return 2
    dumpdir, lang = sys.argv[1], sys.argv[2]
    module = os.path.basename(lang).rsplit(".", 1)[0].upper()
    segs = segments(dumpdir, module)
    if not segs:
        print("no segment dumps for %s in %s" % (module, dumpdir), file=sys.stderr)
        return 1
    oracle = os.environ.get("NEORACLE", "build/neoracle")

    with tempfile.TemporaryDirectory() as work:
        if "--suite" in sys.argv:
            seen = {}
            names = [n for n, _ in SUITE]
            for k, (name, text) in enumerate(SUITE):
                got = named(segs, run(oracle, lang, text, work))
                for a, e in got.items():
                    s = seen.setdefault(a, [0, e[0]])
                    s[0] |= 1 << k
            print("# %s" % " ".join(names))
            for a in sorted(seen):
                mask, how = seen[a]
                tags = " ".join(n for i, n in enumerate(names) if mask >> i & 1)
                print("%08x %08x  %-36s  %s" % (a, mask, how[:36], tags))
        else:
            got = named(segs, run(oracle, lang, "the quick brown fox.", work))
            for a in sorted(got, key=lambda a: got[a][4]):
                how, lo, hi, n, order = got[a]
                print("%08x  order %-5d %8d reads  span %08x..%08x  %s"
                      % (a, order, n, lo, hi, how[:44]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
