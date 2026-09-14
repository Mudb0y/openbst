#!/usr/bin/env python3
"""Reads the duration tables' addresses out of the code that indexes them.

The vowel duration routine names four tables in the same order in every
build -- the durations themselves, the stress scale, the stress addend and
the per-sound addend -- and the transition routine names the pitch offsets
twice, three bytes apart. Taking them from the instruction that reads them is
exact, where carrying an address across builds by a vote is not.
"""

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import capstone
import immdiff
import xmap

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True

ENG_VDUR, ENG_TRANS = 0x10007A90, 0x10007E20
FIELDS = ("vowel_dur", "stress_num", "stress_add", "sound_add")


def refs(path, f, end, lo, hi):
    """The data addresses the code reads, each with the operand's scale."""
    t, _ = xmap.load(path)
    va, code = t
    out = []
    for i in MD.disasm(code[f - va:end - va], f):
        for op in i.operands:
            if op.type != capstone.x86.X86_OP_MEM:
                continue
            v = op.mem.disp & 0xFFFFFFFF
            if lo <= v < hi and (op.mem.base or op.mem.index):
                out.append((v, op.mem.scale if op.mem.index else 1))
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: durtabs.py ENGLISH_DLL DLL...", file=sys.stderr)
        return 2
    ta, ra = xmap.load(sys.argv[1])
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    for path in sys.argv[2:]:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper().replace("DLL_", "")
        tb, rb = xmap.load(path)
        fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
        sig = xmap.match_signatures(fa, fb)
        known = xmap.propagate(fa, fb, xmap.pairs_from_matches(fa, fb, sig))
        pairs = immdiff.fn_pairs(fa, fb, sig, known)
        starts = sorted(xmap.call_targets(tb))
        out = {}
        for fn, want in ((ENG_VDUR, FIELDS), (ENG_TRANS, ("trans_pitch",))):
            g = pairs.get(fn) or known.get(fn)
            if not g:
                continue
            end = min([x for x in starts if x > g] + [g + 0x1000])
            r = refs(path, g, end, rb[0], rb[1])
            if fn == ENG_TRANS:
                # The two reads are three bytes apart; the table is the lower.
                seen = [v for v, _ in r]
                cand = [v for v in seen if v + 3 in seen]
                if cand:
                    out["trans_pitch"] = cand[0]
                continue
            # The three tables read at a stride of two, in order, and the
            # stress scale, which is bytes, between the first two.
            wide = []
            for v, sc in r:
                if sc == 2 and v not in wide:
                    wide.append(v)
            if len(wide) >= 3:
                out["vowel_dur"], out["stress_add"], out["sound_add"] = wide[:3]
                # The stress scale sits a fixed twenty bytes past its addend.
                out["stress_num"] = out["stress_add"] + 0x14
        print("    /* %s */" % name)
        for k in ("trans_pitch",) + FIELDS:
            if k in out:
                print("    .%-17s = 0x%08X," % (k, out[k]))
            else:
                print("    /* %s not found */" % k)
    return 0


if __name__ == "__main__":
    sys.exit(main())
