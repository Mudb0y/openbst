#!/usr/bin/env python3
"""Reports which builds carry which of the 1995 synthesizer tables.

The point is to know how far one back end reaches. Searching for the 1995
tables byte for byte in every other build answers that without reading any of
them: a build that carries the same excitation, gain and duration tables runs
the same lattice, whatever its front end does.

What it finds today: the thirteen 2006 language builds carry the excitation,
gain and duration tables but not the log and antilog pair, so they share the
filter and the gain curve while interpolating differently. The 1998 family
splits the engine in two, with the per-language KGM modules holding the log
tables and the shared KNGMM module holding the excitation and gain tables.
"""

import glob
import os
import sys

TABLES = {
    "pulse":    (0x17E48, 160),
    "noise":    (0x17E08, 32),
    "gain":     (0x18488, 256),
    "log":      (0x17A08, 512),
    "alog":     (0x17C08, 512),
    "duration": (0x18C20, 16),
}


def main():
    if len(sys.argv) < 3:
        print("usage: tablescan.py REFERENCE_DLL DLL_OR_GLOB...", file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    pat = {k: ref[o:o + n] for k, (o, n) in TABLES.items()}

    paths = []
    for a in sys.argv[2:]:
        paths.extend(sorted(glob.glob(a)) or [a])

    for p in paths:
        try:
            d = open(p, "rb").read()
        except OSError as err:
            print("%-24s %s" % (os.path.basename(p), err))
            continue
        hits = [k for k, v in pat.items() if v in d]
        print("%-24s %s" % (os.path.basename(p), ", ".join(hits) if hits else "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
