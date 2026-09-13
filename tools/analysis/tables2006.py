#!/usr/bin/env python3
"""Finds the lattice tables in a build by matching the 1995 build's bytes.

The excitation and gain tables are the same in every build of this family, so
they can be located by searching for them, which is what a per-build profile
would otherwise have to record by hand.
"""

import sys

REF = {"pulse": (0x17E48, 160 * 2), "noise": (0x17E08, 32 * 2),
       "gain": (0x18488, 256 * 2)}


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
    print(",".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
