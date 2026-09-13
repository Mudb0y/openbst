#!/usr/bin/env python3
"""Extracts the letter-to-sound rules.

The rules are the classic NRL notation: a left context, then T marking where
the matched letters begin, the letters themselves, a ")", and a right context.
Within a context, "#" stands for a vowel, "^" for a single consonant, ":" for
zero or more consonants, "%" for a suffix, "+" for a front vowel and a space
for a word boundary.

Each rule is an eight-byte record: a priority, the index of the next rule to
try, an offset to the pattern and an offset to the phonemes it produces.
"""

import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800
RULES = 0x10093000
PATTERNS = 0x1002EFB8
OUTPUTS = 0x10021C80
INDEX = 0x1002E818
DISPATCH = 0x10022ED0


def rule_count(d, off):
    """The table has no terminator, so its extent comes from the highest rule
    index the dispatch table refers to."""
    top = 0
    for i in range(512):
        o = off(INDEX) + i * 4
        entry_off = struct.unpack_from("<h", d, o)[0]
        cnt = d[o + 2]
        if entry_off == -1:
            continue
        if entry_off < 0 or entry_off > 0x4000:
            break
        for k in range(cnt):
            p = off(DISPATCH) + entry_off * 4 + k * 4
            ridx = struct.unpack_from("<h", d, p + 2)[0]
            if 0 <= ridx < 20000:
                top = max(top, ridx)
    return top + 1


def main():
    if len(sys.argv) < 2:
        print("usage: ltsrules.py DLL [limit]", file=sys.stderr)
        return 2
    d = open(sys.argv[1], "rb").read()
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    def off(va):
        return RDATA_OFF + (va - RDATA_VA)

    def cstr(base, delta):
        p = off(base) + delta
        e = d.index(b"\0", p)
        return d[p:e].decode("latin1")

    count = rule_count(d, off)
    shown = 0
    for n in range(count):
        prio, nxt, pat, out = struct.unpack_from("<hhhh", d, off(RULES) + n * 8)
        try:
            pattern = cstr(PATTERNS, pat)
            phonemes = cstr(OUTPUTS, out)
        except (ValueError, IndexError):
            continue
        left, _, rest = pattern.partition("T")
        focus, _, right = rest.partition(")")
        print("%4d  next %5d  %-10s %-10s %-10s -> %s"
              % (n, nxt, left or "-", focus or "-", right or "-",
                 " ".join("%02x" % c for c in phonemes.encode("latin1")) or "(none)"))
        shown += 1
        if limit and shown >= limit:
            break

    print("\n%d rules in the table" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
