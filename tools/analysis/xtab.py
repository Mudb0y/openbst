#!/usr/bin/env python3
"""Cross-references the 1995 build's tables against the other builds.

Every table the reimplementation reads is named here by its 1995 address. For
each one the longest prefix that also occurs in another module is reported,
which separates the tables that are the engine from the tables that are the
language: the first kind is byte-identical everywhere, the second is not there
at all.
"""

import glob
import os
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800

TABLES = [
    ("chattr",     0x10021020, 256),
    ("letterattr", 0x10021120, 256),
    ("casemap",    0x10021220, 256),
    ("symmap",     0x10021320, 256),
    ("duration",   0x10021420, 32),
    ("classtab",   0x10021440, 8),
    ("exctab",     0x10021448, 8),
    ("phattr1",    0x10021648, 128),
    ("phattr2",    0x100216C8, 128),
    ("tokstates",  0x10021748, 512),
    ("voices",     0x10022284, 410),
    ("diphrecs",   0x100292A0, 512),
    ("diphoffs",   0x1002D508, 512),
    ("digits",     0x1002EDF8, 40),
    ("scales",     0x1002EF30, 40),
    ("modmap",     0x10037638, 48),
    ("modtab",     0x10037668, 192),
    ("log",        0x10020208, 512),
    ("alog",       0x10020408, 512),
    ("noise",      0x10020608, 64),
    ("pulse",      0x10020648, 320),
    ("gain",       0x10020C88, 512),
]

STEPS = [512, 256, 128, 64, 32, 16]


def main():
    ref = open(sys.argv[1] if len(sys.argv) > 1 else "dll/1995/B32_TTS.DLL", "rb").read()
    targets = sorted(glob.glob("dll/1998/*.DLL")) + sorted(glob.glob("dll/2006/*.dll"))
    blobs = [(os.path.basename(p), open(p, "rb").read()) for p in targets]

    for name, va, n in TABLES:
        off = RDATA_OFF + (va - RDATA_VA)
        full = ref[off:off + n]
        found = []
        for label, b in blobs:
            best = 0
            at = -1
            for step in STEPS:
                if step > n:
                    continue
                i = b.find(full[:step])
                if i >= 0:
                    best, at = step, i
                    break
            if best:
                found.append("%s@0x%x/%d" % (label, at, best))
        print("%-11s %3d bytes  %s" % (name, n, ", ".join(found) if found else "nowhere"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
