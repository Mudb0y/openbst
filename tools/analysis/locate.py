#!/usr/bin/env python3
"""Finds the 1995 build's tables in another build.

Signature matching alone is not enough: a table whose first bytes differ still
matches from its third byte on, and reporting that as the table's address is
wrong by two. So every sixteen-byte window of the reference table votes for a
base address, and the base with the most votes wins. The score that comes back
is how many of the table's bytes agree at that base, which is what says whether
the table is the same table or merely a near neighbour.

Addresses come back in whatever the target's own addressing is: a virtual
address for a PE build, segment and offset packed as (segment << 16) | offset
for a 16-bit NE one.
"""

import collections
import difflib
import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800

# Each entry is a name, its 1995 virtual address and its length.
TABLES = [
    ("coefgain",     0x10020000, 0x208),
    ("log",          0x10020208, 512),
    ("alog",         0x10020408, 512),
    ("noise",        0x10020608, 64),
    ("pulse",        0x10020648, 320),
    ("suffix_ptrs",  0x10020E90, 16),
    ("code_medial",  0x10020EA0, 112),
    ("code_initial", 0x10020F10, 112),
    ("chattr",       0x10021020, 256),
    ("letterattr",   0x10021120, 256),
    ("casemap",      0x10021220, 256),
    ("symmap",       0x10021320, 256),
    ("basedur",      0x10021420, 32),
    ("phattr1",      0x10021648, 128),
    ("phattr2",      0x100216C8, 128),
    ("voices",       0x10022284, 410),
    ("trans_pitch",  0x10022B58, 592),
    ("vowel_dur",    0x10022DA8, 288),
    ("diph_records", 0x100292A0, 4096),
    ("diph_offsets", 0x1002D508, 4880),
    ("patterns",     0x1002EFB8, 1024),
    ("outputs",      0x10021C80, 1024),
]


def sections_pe(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 2] != b"PE":
        return None
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opthdr = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        h = pe + 24 + opthdr + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", d, h + 8)
        out.append((base + va, max(vs, rs), ro, rs))
    return out


def sections_ne(d):
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    if d[ne:ne + 2] != b"NE":
        return None
    nseg = struct.unpack_from("<H", d, ne + 0x1C)[0]
    seg_off = struct.unpack_from("<H", d, ne + 0x22)[0]
    shift = struct.unpack_from("<H", d, ne + 0x32)[0] or 9
    out = []
    for i in range(nseg):
        sector, length, flags, alloc = struct.unpack_from(
            "<HHHH", d, ne + seg_off + i * 8)
        out.append((((i + 1) << 16), length or 0x10000,
                    sector << shift, length or 0x10000))
    return out


def to_addr(secs, off):
    for va, vsize, raw, rawsize in secs:
        if raw and raw <= off < raw + rawsize:
            return va + (off - raw)
    return None


def locate(ref, target, start, length, window=16, step=4, minvotes=3):
    votes = collections.Counter()
    for r in range(0, max(1, length - window + 1), step):
        pat = ref[start + r:start + r + window]
        if len(pat) < window or len(set(pat)) < 3:
            continue          # too bland to identify anything
        at = target.find(pat)
        while at >= 0:
            votes[at - r] += 1
            at = target.find(pat, at + 1)
    best, score, count = None, 0, 0
    for base, v in votes.most_common(24):
        if v < minvotes or base < 0 or base + length > len(target):
            continue
        agree = sum(1 for i in range(length)
                    if ref[start + i] == target[base + i])
        if agree > score:
            best, score, count = base, agree, v
    return best, score



# ---- the tokeniser transition table --------------------------------------
#
# Three columns of rows: the state the row belongs to, the address of the
# handler it names, and the state to go to. Only the middle column is an
# address, so the other two are the same in every build and the table can be
# found by them alone -- and once found, row by row, it says which address in
# this build is which handler in the 1995 one.

TOK_1995 = 0x10021748

# The handlers the library names, by their 1995 address.
HANDLERS = {
    "LETTER": 0x10009800, "DIGIT": 0x10009810, "EAT1": 0x10009840,
    "SPACE": 0x10009850, "DOT": 0x10009870, "CURRENCY": 0x10009890,
    "PUNCT": 0x100098B0, "DASH": 0x100098D0, "EXPONENT": 0x10009AA0,
    "MODE1": 0x10009AD0, "MODE2": 0x10009AE0, "DEL": 0x10009AF0,
    "OPENER": 0x10009B10, "TILDE": 0x10009B70, "APOSDOT": 0x10009BD0,
    "APOS": 0x10009BF0, "POSSESS": 0x10009C10, "EAT2": 0x10007030,
    "EAT3": 0x10007040, "WORD": 0x10007AC0, "NUMBER": 0x10005E40,
    "DOTTED": 0x10007BA0, "SEP": 0x10009DE0, "GROUPS": 0x10005460,
    "SEPNUM": 0x10005FA0, "MONEY": 0x100046D0, "DASH2": 0x10007650,
    "ORDINAL": 0x10009930, "ORDEMIT": 0x10001B00, "PUNCTOUT": 0x100070A0,
    "DOTOUT": 0x100074F0,
}


def tok_rows(d, base, stride, width, count):
    rows = []
    step = 2 if width == 2 else 4
    for i in range(count):
        p = base + i * stride
        if p + stride > len(d):
            return None
        f = [int.from_bytes(d[p + k * step:p + (k + 1) * step], "little")
             for k in range(3)]
        rows.append(tuple(f))
    return rows


def ref_tok(ref, code_lo=0x10001000, code_hi=0x10019000):
    base = RDATA_OFF + (TOK_1995 - RDATA_VA)
    rows = []
    i = 0
    while True:
        p = base + i * 12
        h = struct.unpack_from("<I", ref, p + 4)[0]
        if not code_lo <= h < code_hi:
            break
        rows.append(struct.unpack_from("<III", ref, p))
        i += 1
    return rows


def code_range(d, secs):
    """Where the executable section is, which is how a transition row is told
    from whatever follows the last one."""
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 2] == b"PE":
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opthdr = struct.unpack_from("<H", d, pe + 20)[0]
        base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        for i in range(nsec):
            h = pe + 24 + opthdr + i * 40
            flags = struct.unpack_from("<I", d, h + 36)[0]
            vs, va, rs, ro = struct.unpack_from("<IIII", d, h + 8)
            if flags & 0x20000000:
                return base + va, base + va + max(vs, rs)
    return 0, 0


# How a transition row is laid out in each generation: the stride, then the
# offset and width of the state, handler and next-state columns. The 16-bit
# build packs the two state columns into bytes and the handler into a near
# pointer, and puts a link to the following row between them.
ROWFMT_32 = (12, 0, 4, 4, 4, 8, 4)
ROWFMT_16 = (6, 0, 1, 1, 2, 5, 1)


def read_row(d, p, fmt):
    stride, so, sw, ho, hw, no, nw = fmt
    if p + stride > len(d):
        return None
    return (int.from_bytes(d[p + so:p + so + sw], "little"),
            int.from_bytes(d[p + ho:p + ho + hw], "little"),
            int.from_bytes(d[p + no:p + no + nw], "little"))


def find_tok(ref_rows, d, fmt):
    """Anchors the table by the longest run of rows whose state and next
    columns match the reference's. Those two columns are the same in every
    build; the handler column is the one thing that moves."""
    want = [(r[0], r[2]) for r in ref_rows]
    stride = fmt[0]
    n = len(want)
    best = (None, 0)
    for base in range(0, len(d) - stride * 4):
        r = read_row(d, base, fmt)
        if not r or (r[0], r[2]) != want[0]:
            continue
        got = 0
        for i in range(n):
            r = read_row(d, base + i * stride, fmt)
            if not r or (r[0], r[2]) != want[i]:
                break
            got += 1
        if got > best[1]:
            best = (base, got)
    return best


def tok_table(d, base, fmt, code_lo, code_hi, limit=1024):
    """Every row of the target's table, read until the handler column stops
    naming code."""
    rows = []
    for i in range(limit):
        r = read_row(d, base + i * fmt[0], fmt)
        if not r:
            break
        if fmt[4] == 4 and not code_lo <= r[1] < code_hi:
            break
        if fmt[4] == 2 and (r[1] == 0 or r[1] >= code_hi):
            break
        rows.append(r)
    return rows


def map_handlers(ref_rows, tgt_rows):
    """Aligns the two tables row by row on their state and next columns, then
    reads the handler mapping off the aligned pairs. Alignment rather than
    plain matching, because a build that adds or drops a state shifts every
    row after it and matching by key alone then pairs the wrong rows."""
    a = [(r[0], r[2]) for r in ref_rows]
    b = [(r[0], r[2]) for r in tgt_rows]
    sm = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
    votes = collections.defaultdict(collections.Counter)
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            votes[ref_rows[i + k][1]][tgt_rows[j + k][1]] += 1
    out = {}
    for h, c in votes.items():
        (best, n), = c.most_common(1)
        out[h] = (best, n, sum(c.values()))
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: locate.py REFERENCE TARGET...", file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    for path in sys.argv[2:]:
        d = open(path, "rb").read()
        secs = sections_pe(d) or sections_ne(d)
        if not secs:
            print("%s: neither PE nor NE" % path)
            continue
        print("%s" % path)
        is_ne = sections_ne(d) and not sections_pe(d)
        fmt = ROWFMT_16 if is_ne else ROWFMT_32
        clo, chi = code_range(d, secs)
        if is_ne:
            clo, chi = 0, secs[0][1]
        rr = ref_tok(ref)
        base, got = find_tok(rr, d, fmt)
        if base is None:
            print("    %-14s not found" % "tokstates")
        else:
            tgt = tok_table(d, base, fmt, clo, chi)
            addr = to_addr(secs, base)
            print("    %-14s file 0x%06x  addr %s  %d rows here, %d of the "
                  "reference's %d in the same order"
                  % ("tokstates", base,
                     "0x%08x" % addr if addr is not None else "unmapped",
                     len(tgt), got, len(rr)))
            m = map_handlers(rr, tgt)
            named = {v: k for k, v in HANDLERS.items()}
            print("    handlers: %d of the reference's %d placed"
                  % (len(m), len({r[1] for r in rr})))
            for h in sorted(m):
                th, n, tot = m[h]
                print("      %-12s %08x -> %08x  %d/%d votes"
                      % (named.get(h, ""), h, th, n, tot))

        for name, va, length in TABLES:
            start = RDATA_OFF + (va - RDATA_VA)
            base, score = locate(ref, d, start, length)
            if base is None:
                print("    %-14s not found" % name)
                continue
            addr = to_addr(secs, base)
            print("    %-14s file 0x%06x  addr %s  %d/%d bytes agree"
                  % (name, base,
                     "0x%08x" % addr if addr is not None else "unmapped",
                     score, length))
    return 0


if __name__ == "__main__":
    sys.exit(main())
