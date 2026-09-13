#!/usr/bin/env python3
"""Finds the table behind one feature of the text by reading twice.

A pointer table has no byte pattern to match on, because every pointer in it
has moved, and no distinctive access shape, because a hundred other pointer
tables are read the same way. What it does have is a cause: the slot holding
"cents" is read when the text says a price and not otherwise.

So each probe text is run against a baseline that differs from it in one
thing, and the addresses the probe reads that the baseline does not are the
tables that thing needs. Usually there are one or two.
"""

import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tabtrace


def sections(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    lo, hi = None, 0
    for i in range(nsec):
        h = pe + 24 + oh + i * 40
        flags = struct.unpack_from("<I", d, h + 36)[0]
        vs, va, rs, ro = struct.unpack_from("<IIII", d, h + 8)
        if ro and not (flags & 0x20000000):
            if lo is None or base + va < lo:
                lo = base + va
            hi = max(hi, base + va + max(vs, rs))
    return lo, hi


def run(oracle, dll, text, calls, work, drange, text_img, imms):
    """The set of addresses the run named, taken from the instructions that did
    the reading rather than from the addresses themselves: an instruction names
    the table's base, an address names a row of it."""
    trace = os.path.join(work, "t.bin")
    cmd = [oracle, "--dll", dll, "--limit", "900000000", "--trace", trace,
           "--raw", "--out", os.devnull]
    for c in calls:
        cmd += ["--call", c.replace("{}", text)] if "{}" in c else ["--call", c]
    if not calls:
        cmd += ["--speak", text]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lo, hi = drange
    pcs = set()
    spans = {}
    with open(trace, "rb") as f:
        d = f.read()
    for i in range(len(d) // 12):
        a, p = struct.unpack_from("<II", d, i * 12)
        if lo <= a < hi:
            pcs.add(p)
            s = spans.get(p)
            if s is None:
                spans[p] = [a, a]
            else:
                if a < s[0]:
                    s[0] = a
                if a > s[1]:
                    s[1] = a
    os.unlink(trace)
    out = {}
    for p in pcs:
        got = tabtrace.displacement(text_img, p, drange)
        if got:
            out[got[0]] = (got[1] + " " + got[2], spans[p])
        else:
            # Read through a register: the base is whatever the code loaded
            # into it, which is the largest address it names that the read did
            # not go below.
            b = tabtrace.base_below(imms, spans[p][0])
            if b is not None:
                out.setdefault(b, ("via a register", spans[p]))
    return out


# One line of text per thing the engine can be made to do, chosen so that each
# table the locator cannot place is brought in by a different combination of
# them. Two builds agree on which combination, whatever their addresses.
SUITE = [
    ("plain",    "a."),
    ("dict",     "dog."),
    ("rules",    "zzyzx."),
    ("except",   "the."),
    ("stress",   "banana."),
    ("suffix",   "hoped."),
    ("one",      "1."),
    ("teen",     "13."),
    ("ten",      "20."),
    ("oh",       "105."),
    ("hundred",  "300."),
    ("scale",    "1000000."),
    ("zero",     "0."),
    ("group",    "1,234."),
    ("point",    "1.5."),
    ("money",    "$4.50"),
    ("ord_st",   "1st."),
    ("ord_nd",   "2nd."),
    ("ord_rd",   "3rd."),
    ("ord_th",   "4th."),
    ("ord_fifth", "5th."),
    ("ord_tieth", "20th."),
    ("ord_first", "21st."),
    ("charname", "@."),
    ("dotted",   "A.B.C."),
    ("dash",     "co-op."),
    ("spell",    "12345678901."),
]


def suite(dll, oracle, calls):
    drange = sections(dll)
    text_img, full = tabtrace.load_text(dll)
    imms = tabtrace.image_immediates(text_img, drange)
    seen = {}
    names = [n for n, _ in SUITE]
    with tempfile.TemporaryDirectory() as work:
        for k, (name, text) in enumerate(SUITE):
            got = run(oracle, dll, text, calls, work, drange, text_img, imms)
            for a, (how, span) in got.items():
                e = seen.setdefault(a, [0, how])
                e[0] |= 1 << k
    return names, seen


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--suite":
        oracle = os.environ.get("ORACLE", "build/oracle")
        calls = [c for c in os.environ.get("ORACLE_CALLS", "").split(",") if c]
        names, seen = suite(sys.argv[2], oracle, calls)
        print("# %s" % " ".join(names))
        for a in sorted(seen):
            mask, how = seen[a]
            tags = " ".join(n for i, n in enumerate(names) if mask >> i & 1)
            print("%08x %08x  %-40s  %s" % (a, mask, how[:40], tags))
        return 0
    if len(sys.argv) < 4:
        print("usage: difftrace.py DLL BASELINE PROBE[::NAME]...\n"
              "       difftrace.py --suite DLL\n"
              "  set ORACLE_CALLS to drive a 2006 build, e.g.\n"
              "  ORACLE_CALLS='Init_TTS,Say_TTS:str:{}'", file=sys.stderr)
        return 2
    dll = sys.argv[1]
    baseline = sys.argv[2]
    probes = sys.argv[3:]
    oracle = os.environ.get("ORACLE", "build/oracle")
    calls = [c for c in os.environ.get("ORACLE_CALLS", "").split(",") if c]
    drange = sections(dll)
    text_img, _ = tabtrace.load_text(dll)

    with tempfile.TemporaryDirectory() as work:
        imms = tabtrace.image_immediates(text_img, drange)
        base = run(oracle, dll, baseline, calls, work, drange, text_img, imms)
        print("baseline %r names %d tables" % (baseline, len(base)))
        for p in probes:
            text, _, name = p.partition("::")
            got = run(oracle, dll, text, calls, work, drange, text_img, imms)
            new = sorted(set(got) - set(base))
            print("%-14s %-24r %d new" % (name or "", text, len(new)))
            for a in new:
                how, span = got[a]
                print("    %08x  read %08x..%08x  %s" % (a, span[0], span[1], how[:52]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
