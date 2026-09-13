#!/usr/bin/env python3
"""Extracts the acoustic target table from a BeSTspeech image.

Frames that sit on a steady portion of a phoneme are copied straight out of
this table; frames mid-transition are interpolated and appear nowhere in the
binary. Each entry is ten signed bytes, doubled by the engine into the Q8
reflection coefficients the lattice consumes.

Layout, recovered from the fetch routine's index arithmetic:
    entry = base + (voice * 410 + slot) * 10
    slot  = phoneme * 10 + variant
The voice index is validated to 1..6 by the engine, with 0 meaning default.
"""

import struct
import sys

BASE_VA = 0x10022284
RDATA_VA = 0x10020000
RDATA_OFF = 0x17800
SLOTS_PER_VOICE = 410
VARIANTS = 10
ENTRY = 10


def file_off(va):
    return RDATA_OFF + (va - RDATA_VA)


def main():
    if len(sys.argv) < 2:
        print("usage: voicetable.py DLL [voice] [phoneme]", file=sys.stderr)
        return 2
    d = open(sys.argv[1], "rb").read()
    voice = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    only = int(sys.argv[3]) if len(sys.argv) > 3 else None

    if not 1 <= voice <= 6:
        print("voice must be 1..6", file=sys.stderr)
        return 2

    print("voice %d, base VA 0x%08x" % (voice, BASE_VA))
    nph = SLOTS_PER_VOICE // VARIANTS

    for ph in range(nph):
        if only is not None and ph != only:
            continue
        rows = []
        for var in range(VARIANTS):
            slot = ph * VARIANTS + var
            off = file_off(BASE_VA + (voice * SLOTS_PER_VOICE + slot) * ENTRY)
            k = struct.unpack_from("<10b", d, off)
            rows.append(k)

        if all(all(v == 0 for v in r) for r in rows):
            continue

        print("phoneme %2d" % ph)
        for var, k in enumerate(rows):
            if all(v == 0 for v in k):
                continue
            # The engine doubles each byte into Q8, so 256 is unity.
            q8 = [v * 2 for v in k]
            print("   var %d  %s" % (var, " ".join("%5d" % v for v in q8)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
