#!/usr/bin/env python3
"""Checks sentence assembly against the engine.

Captures the token sequence and the phrase stream each run of tokens produces,
and requires our assembler to build the same stream from the same tokens.
"""

import subprocess
import sys

TOK, PHRASE = 0x1000E49D, 0x1001314E
KIND, BUF, EMPH, MODE = 0x1001C036, 0x10019330, 0x10019A5E, 0x1001B46D
STREAM, LENADDR = 0x1001E3C0, 0x1001B69C


def le(parts):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        r = subprocess.run(
            [oracle, "--dll", dll, "--speak", text,
             "--snap", "0x%x:0x%x:1,0x%x:96,0x%x:1,0x%x:1"
                       % (TOK, KIND, BUF, EMPH, MODE),
             "--snap", "0x%x:0x%x:256,0x%x:2" % (PHRASE, STREAM, LENADDR)],
            capture_output=True, text=True)
        groups, cur, want = [], [], []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                cur.append((le(p[0]), le(p[2]), le(p[3]), p[1]))
            elif l.startswith("U | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                groups.append((cur, p[0][:le(p[1])]))
                cur = []
        if not groups:
            continue
        inp = ""
        for toks, _ in groups:
            for kind, emph, mode, buf in toks:
                inp += "K %d %d %d %s\n" % (kind, emph, mode, " ".join(buf))
        q = subprocess.run([synth, dll], input=inp, capture_output=True, text=True)
        mine = [l.split() for l in q.stdout.splitlines()]
        for (toks, w), got in zip(groups, mine):
            if w == got:
                same += 1
            else:
                diff += 1
                if shown < 3:
                    shown += 1
                    print("  DIFFER: %s" % text)
                    print("    engine %s" % " ".join(w[:40]))
                    print("    ours   %s" % " ".join(got[:40]))
        if len(mine) < len(groups):
            diff += len(groups) - len(mine)
    print("asmtest: %d phrases identical, %d differing" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
