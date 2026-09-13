#!/usr/bin/env python3
"""Checks the middle of the engine: diphone expansion to acoustic targets.

For each line of a corpus, captures the (position count, allophone index) pairs
the engine pushes, expands each index through the diphone table for that many
positions, and requires the resulting target sequence to match the targets the
frame builder actually fetched.
"""

import re
import struct
import subprocess
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800
RECORDS, OFFSETS, OFFSETS_END = 0x100292A0, 0x1002D508, 0x1002E818
LENGTHS = {0: 1, 1: 3, 2: 1, 3: 3, 4: 3, 5: 5}


def collapse(seq):
    return [v for i, v in enumerate(seq) if i == 0 or v != seq[i - 1]]


def main():
    oracle, dll, corpus = sys.argv[1], sys.argv[2], sys.argv[3]
    d = open(dll, "rb").read()

    def off(va):
        return RDATA_OFF + (va - RDATA_VA)

    n = (off(OFFSETS_END) - off(OFFSETS)) // 2
    table = struct.unpack_from("<%dH" % n, d, off(OFFSETS))

    def expand(idx, positions):
        if not 0 <= idx < n or not table[idx]:
            return []
        p = off(RECORDS) + table[idx]
        out = []
        # A count of zero means the entry contributes nothing at all.
        for _ in range(positions):
            for _ in range(32):
                b = d[p]
                t = (b & 0x70) >> 4
                ln = LENGTHS.get(t, 1)
                rec = d[p:p + ln]
                if t in (1, 3, 4, 5) and ln >= 3:
                    out.append(((rec[2] << 8) | rec[1]) & 0x1FF)
                if t == 5 and ln >= 5:
                    out.append(((rec[4] << 8) | rec[3]) & 0x1FF)
                p += ln
                if b & 0x80:
                    break
        return out

    def run(text, hook):
        r = subprocess.run([oracle, "--dll", dll, "--speak", text, "--hook", hook],
                           capture_output=True, text=True)
        return r.stdout

    same = diff = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        pairs = [(int(a), int(b)) for a, b in
                 re.findall(r"^call \S+ (\S+) (\S+)", run(text, "0x10002910:4"), re.M)]
        if not pairs:
            continue
        # Entries carrying the command flag in the top nibble are not expanded
        # by the engine, so they contribute no targets.
        ours = collapse([t for cnt, idx in pairs
                         if (idx & 0xF000) == 0
                         for t in expand(idx, cnt)])
        theirs = collapse([int(x) for x in
                           re.findall(r"^call \S+ \S+ (\S+)", run(text, "0x10004130:2"), re.M)])
        if ours == theirs:
            same += 1
        else:
            diff += 1
            if diff <= 3:
                print("  DIFFER: %s" % text)
                print("    ours  : %s" % " ".join(map(str, ours[:24])))
                print("    theirs: %s" % " ".join(map(str, theirs[:24])))

    print("segtest: %d target sequences identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
