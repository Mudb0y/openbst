#!/usr/bin/env python3
"""Checks the transition-order model.

Around each sound the engine emits transition events at up to three positions,
and how many come before the sound and how many after depends on the sound's
variant code. A running counter accumulates those events; when the sound itself
is emitted the counter becomes its position count and resets.

So the count the engine records for each sound is the events after the previous
sound plus the events before this one, and that is what this measures:
predicted counts against the ones the engine actually pushed.

This is an analysis aid, not a test. It currently accounts for 868 of 976
counts. The residue is consistently one too high, which points at sounds that
reach the emitter but skip the ordering switch, contributing no trailing
events; those guards are not modelled yet.
"""

import re
import subprocess
import sys


def before_after(v):
    if v == 0:
        return None                 # emits nothing at all
    if 1 <= v <= 0x0D:
        return 1, 2
    if 0x0E <= v <= 0x10:
        return 0, 1
    if v in (0x11, 0x12):
        return 1, 2
    if v in (0x13, 0x14, 0x15, 0x17, 0x1B, 0x1D):
        return 1, 1
    if v in (0x16, 0x18, 0x1C):
        return 1, 2
    if v in (0x19, 0x1A):
        return 1, 0
    if 0x1E <= v <= 0x2E:
        return 1, 2
    if v == 0x2F:
        return 0, 1
    if v == 0x30:
        return 0, 0
    return None


def main():
    oracle, dll, corpus = sys.argv[1], sys.argv[2], sys.argv[3]
    ok = bad = lines = 0

    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        out = subprocess.run([oracle, "--dll", dll, "--speak", text,
                              "--hook", "0x10002910:4"],
                             capture_output=True, text=True).stdout
        pairs = [(int(a), int(b)) for a, b in
                 re.findall(r"^call \S+ (\S+) (\S+)", out, re.M)]
        if not pairs:
            continue
        lines += 1

        acc = 0
        for count, idx in pairs:
            if idx & 0xF000:
                continue
            v = idx % 48
            ba = before_after(v)
            if ba is None:
                bad += 1
                continue
            b, a = ba
            acc += b
            if acc == count:
                ok += 1
            else:
                bad += 1
                if bad <= 12:
                    print("  variant 0x%02x: predicted %d, engine %d" % (v, acc, count))
            acc = a

    print("counttest: %d counts predicted, %d differing, over %d lines"
          % (ok, bad, lines))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
