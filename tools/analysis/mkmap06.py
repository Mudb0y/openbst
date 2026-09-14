#!/usr/bin/env python3
"""Writes a table directory for one of the 2006 language builds.

The thirteen builds are one body of code compiled thirteen times, so a
function in one has the same instructions as the function in the other and
differs only in the addresses it names. Pairing the functions by the shape of
their instructions and then walking the two in step gives, for every address
the English build's directory holds, the address the other build puts there.

What the code never names -- a table only reached through another one, or a
pointer table whose entries the code reads but whose base it computes -- is
placed by finding what it points at, since the phoneme strings are the same
bytes in every build whose sounds are numbered the same way.

What it cannot place, it says.
"""

import re
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import xmap


def sections(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    oh = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + oh + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", d, o + 8)
        secs.append((base + va, max(vs, rs), ro, rs))
    return d, secs


class Img:
    def __init__(self, path):
        self.d, self.secs = sections(path)

    def off(self, va):
        for sva, sz, ro, rs in self.secs:
            if sva <= va < sva + sz:
                f = ro + (va - sva)
                return f if va - sva < rs else None
        return None

    def va(self, off):
        for sva, sz, ro, rs in self.secs:
            if ro <= off < ro + rs:
                return sva + (off - ro)
        return None

    def read(self, va, n):
        o = self.off(va)
        return self.d[o:o + n] if o is not None else b""

    def find(self, pat):
        return [self.va(m.start()) for m in re.finditer(re.escape(pat), self.d)
                if self.va(m.start()) is not None]


# The English build's directory, as C source, is the reference. Reading it
# rather than repeating it here keeps the two from drifting apart.
def read_reference(src, name):
    text = open(src).read()
    i = text.index("const bst_tabmap %s = {" % name)
    j = text.index("\n};", i)
    body = text[i:j]
    out = {}
    for m in re.finditer(r"\.(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", body):
        out.setdefault(m.group(1), int(m.group(2), 0))
    for field in ("bucket_index", "bucket_data"):
        m = re.search(r"\.%s\s*=\s*\{(.*?)\}" % field, body, re.S)
        out[field] = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    for field in ("h", "s"):
        m = re.search(r"\.%s\s*=\s*\{(.*?)\n    \}" % field, body, re.S)
        out[field] = dict((k, int(v, 0)) for k, v in
                          re.findall(r"\[(\w+)\]\s*=\s*(0x[0-9A-Fa-f]+)", m.group(1)))
    return out


def strat(img, va, maxlen=64):
    s = img.read(va, maxlen)
    i = s.find(b"\0")
    return s[:i] if i >= 0 else s


def place_by_pointee(a, b, va, n, bias=0):
    """The address in b of an n-entry pointer table that a keeps at va."""
    src = struct.unpack_from("<%dI" % n, a.read(va, 4 * n))
    votes = {}
    for i, sp in enumerate(src):
        if not sp or a.off(sp) is None:
            continue
        s = strat(a, sp)
        if len(s) < 3:
            continue
        for c in b.find(s + b"\0"):
            for h in b.find(struct.pack("<I", c)):
                base = h - 4 * (i + bias)
                votes[base] = votes.get(base, 0) + 1
    if not votes:
        return None, 0
    best = max(votes, key=votes.get)
    return best, votes[best]


SCALARS = ("tok_stride tok_state_off tok_state_w tok_handler_off tok_handler_w "
           "tok_next_off tok_next_w lts_index_stride rule_stride rule_prio_w "
           "trie_st_off trie_li_off trie_base_off trie_max_off trie_po_off "
           "silence_f0 unvoiced_dur unvoiced_reps min_period interp_round_mask "
           "coef_round_mask class_shift long_silence_f0 exc_two_way "
           "unvoiced_chunk no_breath_break no_closing_phrase dur_mult "
           "nearest_round pitch_rate slope_mult inton_dur_mult "
           "inton_slope_shift stress_shift voice_span voice_stride "
           "vowel_dur_shift trn_round ph_single_n ph_pair_n "
           "close_pause comma_ends_text contour_round dur_frac_shift "
           "possessive_is sepnum_first_three no_suffix trn_whole "
           "code_lo code_hi").split()

ADDRS = ("chattr letterattr casemap symmap phattr1 phattr2 classtab exctab "
         "basedur coefgain tokstates names code_medial code_initial "
         "ph_single ph_pair suffix_ptrs lts_index dispatch rules patterns "
         "outputs trie_desc modmap modtab trans_pitch vowel_dur stress_num "
         "stress_add sound_add diph_records diph_offsets diph_offsets_end "
         "voices").split()


def rows(img, base, lo, hi, stride=12):
    """The transition table, as far as its handler column stays in the code."""
    out = []
    i = 0
    while True:
        d = img.read(base + stride * i, stride)
        if len(d) < stride:
            break
        st, h, nx = struct.unpack("<III", d[:12])
        if not (lo <= h < hi):
            break
        out.append((st, h, nx))
        i += 1
    return out


def carry_handlers(a, b, aref, bref, want):
    """Handler addresses, from aligning the two transition tables. Both are
    the same machine with the same states in the same order, so the state and
    next columns line them up and the handler column falls out."""
    import collections
    import difflib
    ra = rows(a, aref, 0x10001000, 0x10015000)
    rb = rows(b, bref, 0x10001000, 0x10015000)
    ka = [(x[0], x[2]) for x in ra]
    kb = [(x[0], x[2]) for x in rb]
    sm = difflib.SequenceMatcher(a=ka, b=kb, autojunk=False)
    votes = collections.defaultdict(collections.Counter)
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            votes[ra[i + k][1]][rb[j + k][1]] += 1
    out = {}
    for h in want:
        c = votes.get(h)
        if c:
            out[h] = c.most_common(1)[0][0]
    return out


MD = None


def instrs(text, drange, start, stop):
    """One function, as (address, shape, immediates)."""
    global MD
    if MD is None:
        import capstone
        MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        MD.detail = True
    import capstone
    va, t = text
    lo, hi = drange
    out = []
    for ins in MD.disasm(t[start - va:stop - va], start):
        shape = [ins.mnemonic]
        imms = []
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_REG:
                shape.append("r%d" % op.reg)
            elif op.type == capstone.x86.X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
                shape.append("A" if lo <= v < hi else "i")
                if not (lo <= v < hi):
                    imms.append(op.imm)
            else:
                v = op.mem.disp & 0xFFFFFFFF
                shape.append("m%d:%d:%s" % (op.mem.base, op.mem.index,
                                            "A" if lo <= v < hi else "d"))
                if not (lo <= v < hi):
                    imms.append(op.mem.disp)
        out.append((ins.address, " ".join(shape), imms))
    return out


def carry_immediate(ta, ra, tb, rb, fa, fb, matches, site, want):
    """The value the other build has where this one has `want` at `site`."""
    import difflib
    owner = max((f for f in fa if f <= site), default=None)
    if owner is None or owner not in matches:
        return None
    other = matches[owner]
    ea = min([f for f in fa if f > owner], default=ta[0] + len(ta[1]))
    eb = min([f for f in fb if f > other], default=tb[0] + len(tb[1]))
    ia = instrs(ta, ra, owner, min(ea, owner + 0x1000))
    ib = instrs(tb, rb, other, min(eb, other + 0x1000))
    k = next((i for i, x in enumerate(ia) if x[0] == site), None)
    if k is None:
        return None
    sm = difflib.SequenceMatcher(a=[x[1] for x in ia], b=[x[1] for x in ib],
                                 autojunk=False)
    for i, j, n in sm.get_matching_blocks():
        if i <= k < i + n:
            for v in ib[j + (k - i)][2]:
                if 0 <= v < 0x100:
                    return v
    return None


def main():
    if len(sys.argv) < 3:
        print("usage: mkmap06.py ENGLISH_DLL DLL [DLL...]", file=sys.stderr)
        return 2
    ref = read_reference("src/text.c", "BST_MAP_2006_ENG")
    ta, ra = xmap.load(sys.argv[1])
    fa = xmap.disasm_fns(ta, ra, xmap.call_targets(ta))
    a = Img(sys.argv[1])

    for path in sys.argv[2:]:
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper().replace("DLL_", "")
        tb, rb = xmap.load(path)
        fb = xmap.disasm_fns(tb, rb, xmap.call_targets(tb))
        b = Img(path)
        sig = xmap.match_signatures(fa, fb)
        known = xmap.pairs_from_matches(fa, fb, sig)
        known = xmap.propagate(fa, fb, known)
        # A build with extra code of its own pairs fewer functions outright,
        # so a second pass accepts a function that shares only one address
        # with its candidate.
        if len(known) < 2000:
            known = xmap.propagate(fa, fb, known, minshared=1)
        print("/* %s: %d functions matched, %d addresses carried */" % (name, len(sig), len(known)))

        gaps = []
        out = {}
        for k in ADDRS:
            v = ref.get(k, 0)
            if not v:
                continue
            if v in known:
                out[k] = known[v]
            else:
                gaps.append(k)
        if "diph_offsets" in out and "diph_offsets_end" not in out:
            n = (ref["diph_offsets_end"] - ref["diph_offsets"]) // 2
            out["diph_offsets_end"] = out["diph_offsets"] + 2 * n
            gaps.remove("diph_offsets_end")

        out["h"] = {}
        hmap = {}
        if "tokstates" in out:
            hmap = carry_handlers(a, b, ref["tokstates"], out["tokstates"],
                                  set(ref["h"].values()))
        for k, v in ref["h"].items():
            if v in known:
                out["h"][k] = known[v]
            elif v in hmap:
                out["h"][k] = hmap[v]
            else:
                gaps.append("h." + k)
        out["s"] = {}
        # The number words are one block; the single words another. Both are
        # found by what they point at, since a phoneme string is the same
        # bytes in any build whose sounds are numbered alike.
        ARRAYS = {"BST_S_DIGITS": (0x30, 10), "BST_S_TENS": (0x32, 8),
                  "BST_S_TEENS": (0x30, 10), "BST_S_SCALES": (1, 5)}
        for k, v in ref["s"].items():
            if v in known:
                out["s"][k] = known[v]
                continue
            if k in ARRAYS:
                lo, cnt = ARRAYS[k]
                base, votes = place_by_pointee(a, b, v + 4 * lo, cnt, lo)
                if base is not None and votes >= 2:
                    out["s"][k] = base
                    continue
            base, votes = place_by_pointee(a, b, v, 1)
            if base is not None:
                out["s"][k] = base
            else:
                gaps.append("s." + k)
        for f in ("bucket_index", "bucket_data"):
            out[f] = [known.get(v, 0) for v in ref[f]]
            if not all(out[f]):
                gaps.append(f)

        # A block of data moves as a whole between builds, so anything still
        # unplaced takes the shift of the nearest thing that is placed. Only
        # a near neighbour is trusted: far enough away and it is a different
        # block that has moved by a different amount.
        anchors = sorted((ref[k], out[k]) for k in ADDRS if k in out and ref.get(k))
        anchors += sorted((v, out["s"][k]) for k, v in ref["s"].items()
                          if k in out["s"])
        anchors.sort()

        def nearest(v, limit=0x1000):
            best = None
            for src, dst in anchors:
                d = abs(src - v)
                if best is None or d < best[0]:
                    best = (d, dst - src)
            return best[1] if best and best[0] <= limit else None

        still = []
        for k in list(gaps):
            if k.startswith("s."):
                slot = k[2:]
                delta = nearest(ref["s"][slot])
                if delta is not None:
                    out["s"][slot] = ref["s"][slot] + delta
                    continue
            elif not k.startswith("h.") and k in ADDRS:
                delta = nearest(ref[k])
                if delta is not None:
                    out[k] = ref[k] + delta
                    continue
            still.append(k)
        gaps = still

        # The separator mark is written by the assembler as a literal, so the
        # difference between the two builds' literals is the whole shift.
        pairfn = {f: known[f] for f in fa if f in known}
        sep = carry_immediate(ta, ra, tb, rb, fa, fb, pairfn, 0x10002B36, 0x4C)
        shift = (sep - 0x4C) if sep is not None else None
        cmd = carry_immediate(ta, ra, tb, rb, fa, fb, pairfn, 0x1000302B, 0x7C)
        # The tenth byte of the phrase header, written as a literal too.
        shape = carry_immediate(ta, ra, tb, rb, fa, fb, pairfn, 0x10003BFD, 0x4C)

        print("const bst_tabmap BST_MAP_2006_%s = {" % name)
        if shift is not None:
            print("    .%-17s = %d," % ("code_shift", shift))
        else:
            gaps.append("code_shift")
        if cmd is None:
            gaps.append("cmd_code")
        elif shift is None or cmd != ((0x7C + shift) & 0xFF):
            print("    .%-17s = 0x%02X," % ("cmd_code", cmd))
        if shape is None:
            gaps.append("hdr_shape")
        elif shape != 0x4C:
            print("    .%-17s = 0x%02X," % ("hdr_shape", shape))
        for k in ADDRS:
            if k in out:
                print("    .%-17s = 0x%08X," % (k, out[k]))
        for k in SCALARS:
            if ref.get(k):
                print("    .%-17s = %d," % (k, ref[k]))
        for f in ("bucket_index", "bucket_data"):
            print("    .%s = {" % f)
            for i in range(0, len(out[f]), 5):
                print("        " + " ".join("0x%08X," % x for x in out[f][i:i + 5]))
            print("    },")
        for f in ("h", "s"):
            print("    .%s = {" % f)
            for k in sorted(out[f]):
                print("        [%s] = 0x%08X," % (k, out[f][k]))
            print("    },")
        print("};")
        if gaps:
            print("/* not placed: %s */" % " ".join(gaps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
