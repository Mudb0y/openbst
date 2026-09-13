#!/usr/bin/env python3
"""Call graph of a relocated 16-bit code segment.

Functions are found by their prologue, which this compiler emits identically
everywhere, and edges by every near call whose target is one of them. Enough
to answer "what reaches this" without disassembling the whole segment.
"""

import re
import sys

PROLOGUE = re.compile(rb"\x8c\xd8\x90\x45\x55\x8b\xec")


def load(path):
    b = open(path, "rb").read()
    funcs = sorted(m.start() for m in PROLOGUE.finditer(b))
    return b, funcs


def owner(funcs, a):
    prev = None
    for p in funcs:
        if p <= a:
            prev = p
        else:
            break
    return prev


def edges(b, funcs):
    fs = set(funcs)
    out = {}
    for i in range(len(b) - 3):
        if b[i] != 0xE8:
            continue
        rel = int.from_bytes(b[i + 1:i + 3], "little", signed=True)
        t = (i + 3 + rel) & 0xFFFF
        if t in fs:
            out.setdefault(t, set()).add(owner(funcs, i))
    return out


def main():
    if len(sys.argv) < 2:
        print("usage: negraph.py SEGFILE [--callers ADDR] [--callees ADDR]",
              file=sys.stderr)
        return 2
    b, funcs = load(sys.argv[1])
    e = edges(b, funcs)
    args = sys.argv[2:]
    if not args:
        print("%d functions" % len(funcs))
        roots = [f for f in funcs if f not in e]
        print("not called from this segment: %s"
              % ", ".join("0x%04x" % f for f in roots))
        return 0
    mode, addr = args[0], int(args[1], 0)
    if mode == "--callers":
        seen, stack = set(), [addr]
        while stack:
            a = stack.pop()
            for c in sorted(e.get(a, [])):
                if c is None or c in seen:
                    continue
                seen.add(c)
                print("0x%04x reaches 0x%04x" % (c, a))
                stack.append(c)
    else:
        for i in range(len(b) - 3):
            if b[i] != 0xE8 or owner(funcs, i) != addr:
                continue
            rel = int.from_bytes(b[i + 1:i + 3], "little", signed=True)
            t = (i + 3 + rel) & 0xFFFF
            if t in set(funcs):
                print("0x%04x calls 0x%04x at 0x%04x" % (addr, t, i))
    return 0


if __name__ == "__main__":
    sys.exit(main())
