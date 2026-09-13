#!/usr/bin/env python3
"""Extracts the pitch, gain and excitation track from an "oracle --frames" dump.

Frame byte 3 with the low nibble of byte 1 gives a 12-bit pitch period carrying
four fractional bits, so the period in output samples is that value over 16 and
F0 is the sample rate divided by it. Byte 2 indexes the gain table. Bits 4 and
5 of byte 0 select the excitation and its low nibble is the number of pitch
periods the frame is held for.
"""

import sys

RATE = 11025


def frames(text):
    toks = text.split()
    i = 0
    while i < len(toks):
        if toks[i] == "F" and i + 16 < len(toks):
            yield [int(x, 16) for x in toks[i + 1:i + 17]]
            i += 17
        else:
            i += 1


MODES = {0x00: "silence", 0x10: "voiced", 0x20: "noise", 0x30: "mixed"}


def main():
    data = open(sys.argv[1]).read() if len(sys.argv) > 1 else sys.stdin.read()

    t = 0.0
    prev = None
    print("%7s %4s %-8s %6s %7s %5s" % ("time", "rep", "mode", "gain", "F0", "samples"))
    for f in frames(data):
        rep = f[0] & 0x0F
        mode = MODES.get(f[0] & 0x30, "?")
        gain = f[2]
        period = ((f[3] << 4) | (f[1] & 0x0F)) / 16.0
        n = int(period * rep) if period else 0
        f0 = RATE / period if period else 0.0

        # Collapse runs that repeat, which is most of a steady phoneme.
        key = (rep, mode, gain, round(f0, 1))
        if key != prev:
            print("%7.3f %4d %-8s %6d %7.1f %5d"
                  % (t, rep, mode, gain, f0, n))
            prev = key
        t += n / RATE
    print("\ntotal %.3f s" % t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
