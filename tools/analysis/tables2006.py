#!/usr/bin/env python3
"""Finds the lattice tables in a build by matching the 1995 build's bytes.

The excitation and gain tables are the same in every build of this family, so
they can be located by searching for them, which is what a per-build profile
would otherwise have to record by hand.
"""

import sys

REF = {"pulse": (0x17E48, 160 * 2), "noise": (0x17E08, 32 * 2),
       "gain": (0x18488, 256 * 2)}

# The log pair is the same numbers, but the 2006 builds keep one byte to an
# entry where the 1995 build keeps a word, so they are searched for narrowed.
NARROW = {"log": (0x17A08, 256), "alog": (0x17C08, 256)}


def main():
    if len(sys.argv) < 3:
        print("usage: tables2006.py REFERENCE_DLL DLL", file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    d = open(sys.argv[2], "rb").read()
    out = []
    for k in ("pulse", "noise", "gain"):
        o, n = REF[k]
        at = d.find(ref[o:o + n])
        if at < 0:
            print("%s not found" % k, file=sys.stderr)
            return 1
        out.append("%x" % at)
    for k in ("log", "alog"):
        o, n = NARROW[k]
        pat = bytes(ref[o + 2 * i] for i in range(n))
        at = d.find(pat)
        if at < 0:
            print("%s not found" % k, file=sys.stderr)
            return 1
        out.append("%x" % at)
    # duration, then the three shape flags: byte-wide log pair, the lattice's
    # output shift, and the noise generator.
    out += ["0", "1", "5", "1"]
    print(",".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
