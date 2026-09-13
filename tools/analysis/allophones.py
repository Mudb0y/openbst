#!/usr/bin/env python3
"""Extracts the allophone table: the link between phonemes and acoustic targets.

Each allophone expands to a short stored sequence of segment records, and it is
those records that carry the acoustic target indices the frame builder fetches.
So the middle of the engine is data too: the algorithm around it computes
durations and picks which allophone applies, but the spectral trajectory of
every sound is a table entry.

Records are variable length, chosen by bits 4 to 6 of the first byte: types 0
and 2 take one byte, types 1, 3 and 4 take three, type 5 takes five. Bit 7 ends
the sequence. Types 1, 3 and 4 carry one nine-bit target index packed across
the two bytes that follow; type 5 carries two, naming both ends of a
transition.

The index of an entry is the previous phoneme times forty-eight plus the
current one, so the table is a diphone inventory: every sound is stored in the
context of the sound before it, which is where the engine's coarticulation
comes from.
"""

import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800
RECORDS = 0x100292A0
OFFSETS = 0x1002D508
OFFSETS_END = 0x1002E818

LENGTHS = {0: 1, 1: 3, 2: 1, 3: 3, 4: 3, 5: 5}


def main():
    if len(sys.argv) < 2:
        print("usage: allophones.py DLL [index...]", file=sys.stderr)
        return 2
    d = open(sys.argv[1], "rb").read()

    def off(va):
        return RDATA_OFF + (va - RDATA_VA)

    count = (off(OFFSETS_END) - off(OFFSETS)) // 2
    table = struct.unpack_from("<%dH" % count, d, off(OFFSETS))
    used = [i for i, v in enumerate(table) if v]

    wanted = [int(a) for a in sys.argv[2:]] or used[:12]
    print("allophone table: %d slots, %d populated, records at 0x%08x"
          % (count, len(used), RECORDS))

    for idx in wanted:
        if not 0 <= idx < count or not table[idx]:
            print("  %4d  empty" % idx)
            continue
        p = off(RECORDS) + table[idx]
        parts = []
        for _ in range(24):
            b = d[p]
            t = (b & 0x70) >> 4
            n = LENGTHS.get(t, 1)
            rec = d[p:p + n]
            note = ""
            if t in (1, 3, 4, 5) and n >= 3:
                note = " target %d" % (((rec[2] << 8) | rec[1]) & 0x1FF)
            if t == 5 and n >= 5:
                # A five-byte record names both ends of a transition.
                note += " target %d" % (((rec[4] << 8) | rec[3]) & 0x1FF)
            parts.append("%s(t%d%s)" % (rec.hex(" "), t, note))
            p += n
            if b & 0x80:
                break
        print("  %4d  %s" % (idx, "  ".join(parts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
