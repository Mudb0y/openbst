#!/usr/bin/env python3
"""Checks the tokeniser against the engine.

Runs our tokeniser over the text and requires the token sequence -- kind and
buffer -- to be the one the engine's own scanner produced.
"""

import subprocess
import sys

TOK = 0x1000E49D
KIND, BUF = 0x1001C036, 0x10019330


def le(parts):
    v = 0
    for i, b in enumerate(parts):
        v |= int(b, 16) << (8 * i)
    return v


def main():
    oracle, synth, dll, corpus = sys.argv[1:5]
    same = diff = 0
    tok_same = tok_tot = 0
    shown = 0
    for line in open(corpus):
        text = line.strip()
        if not text:
            continue
        r = subprocess.run(
            [oracle, "--dll", dll, "--speak", text,
             "--snap", "0x%x:0x%x:1,0x%x:96" % (TOK, KIND, BUF)],
            capture_output=True, text=True)
        want = []
        for l in r.stdout.splitlines():
            if l.startswith("S | "):
                p = [x.strip().split() for x in l[4:].split("|")]
                kind = le(p[0])
                n = 1 if kind == 4 else (int(p[1][0], 16) + 2 if kind == 3 else 6)
                want.append((kind, p[1][:n]))
        if not want:
            continue
        q = subprocess.run([synth, dll], input=text + "\n",
                           capture_output=True, text=True)
        got = []
        for l in q.stdout.splitlines():
            f = l.split()
            if not f:
                continue
            got.append((int(f[0]), f[1:]))
        n = max(len(want), len(got))
        tok_tot += n
        for i in range(n):
            if i < len(want) and i < len(got) and want[i] == got[i]:
                tok_same += 1
        if want == got:
            same += 1
        else:
            diff += 1
            if shown < 3:
                shown += 1
                print("  DIFFER: %s" % text)
                for i in range(max(len(want), len(got))):
                    a = want[i] if i < len(want) else None
                    b = got[i] if i < len(got) else None
                    if a != b:
                        print("    [%d] engine %s" % (i, a))
                        print("        ours   %s" % (b,))
                        break
    print("toktest: %d texts identical, %d differing; %d/%d tokens"
          % (same, diff, tok_same, tok_tot))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
