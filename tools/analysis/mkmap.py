#!/usr/bin/env python3
"""Writes a table directory for a 1998 language module.

Six modules, one body of code, six sets of tables. Everything that can be
settled without reading the code again is settled here:

  - The segment arrangement is the same in all six. Segment one is the code;
    two, three, four, five and six are the letter-to-sound patterns, rules,
    outputs, dispatch and index; seven is the exception trie; the last is the
    automatic data segment and the one before it the diphone inventory; what
    is left in between is the dictionary.

  - The data segment is a handful of blocks that move independently between
    languages but keep their contents in the same order. Matching the 1995
    build's bytes places one table in a block; the rest of the block follows
    from the offsets the English module has.

  - The transition table is found by aligning its state columns against the
    1995 one, and that alignment names the handlers.

  - The dictionary buckets are read out of the switch that selects them, each
    case loading an offset and a segment.

What it cannot place, it says.
"""

import re
import struct
import sys

import capstone

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import locate as L

# Where each table sits in the English module, as an offset in its data
# segment, and which anchor's block it belongs to.
BLOCKS = {
    "coefgain": ("coefgain", 0x055E),
    "log":      ("coefgain", 0x0760),
    "alog":     ("coefgain", 0x0960),
    "basedur":  ("coefgain", 0x0B60),
    "classtab": ("coefgain", 0x0B80),
    "exctab":   ("coefgain", 0x0B86),

    "suffix_ptrs": ("stress_num", 0x005C),
    "code_medial": ("stress_num", 0x0068),
    "code_initial": ("stress_num", 0x00D2),
    "sound_add":  ("stress_num", 0x0154),
    "stress_add": ("stress_num", 0x01B6),
    "stress_num": ("stress_num", 0x01C8),
    "trans_pitch": ("stress_num", 0x01EC),
    "vowel_dur":  ("stress_num", 0x0438),

    "phattr1":   ("ph_single", 0x0EF8),
    "phattr2":   ("ph_single", 0x0F76),
    "ph_single": ("ph_single", 0x0FF4),
    "ph_single_max": ("ph_single", 0x10A0),
    "ph_pair":   ("ph_single", 0x10A2),
    "ph_pair_max": ("ph_single", 0x11DA),

    "chattr":     ("chattr", 0x1476),
    "letterattr": ("chattr", 0x1576),
    "casemap":    ("chattr", 0x1676),
    "symmap":     ("chattr", 0x1776),

    "names":      ("modmap", 0x1D1E),
    "modmap":     ("modmap", 0x2452),
    "modtab":     ("modmap", 0x2654),
}

# The pointer slots that name a spoken word, in the same block as modmap.
STRINGS = {
    "DIGITS": 0x1F76, "TENS": 0x1FE0, "ZERO": 0x2036, "TEENS": 0x2058,
    "SCALES": 0x2140,
    "DOLLARS": 0x23A4, "AND": 0x23A8, "CENTS": 0x23B4, "HUNDRED": 0x23B8,
    "OH": 0x23C0, "POINT": 0x23C4,
    "ORD_ST": 0x23F0, "ORD_ND": 0x23F4, "ORD_RD": 0x23F8, "ORD_FIFTH": 0x23FC,
    "ORD_FIRST": 0x2400, "ORD_TIETH": 0x2404, "ORD_TH": 0x2408,
    "GRPSEP": 0x2410, "PLURAL": 0x2400,
}

# What to match a block on, in order of preference: a table whose bytes are
# the engine rather than the language. Where the first choice is language
# specific after all -- French and Italian rewrite the stress offsets -- the
# next one in the block does instead, and the block's own offsets carry the
# result to the rest.
ANCHORS = {
    "coefgain":   [("coefgain", 0x10020000, 0x208)],
    "stress_num": [("stress_num", 0x10092FD8, 0x28),
                   ("vowel_dur", 0x10022DA8, 288),
                   ("sound_add", 0x10092F58, 0x68),
                   ("code_initial", 0x10020F10, 112)],
    "ph_single":  [("ph_single", 0x10092D68, 0xAC),
                   ("ph_pair", 0x10092E18, 0x138)],
    "chattr":     [("chattr", 0x10021020, 256),
                   ("casemap", 0x10021220, 256),
                   ("symmap", 0x10021320, 256)],
    "modmap":     [("modmap", 0x10037638, 0x30)],
}

# modmap's bytes are the same in every build but short enough that the default
# search will not commit to them, so it gets a narrower window.
FINE = {"modmap"}


def segments(d):
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    nseg = struct.unpack_from("<H", d, ne + 0x1C)[0]
    so = struct.unpack_from("<H", d, ne + 0x22)[0]
    sh = struct.unpack_from("<H", d, ne + 0x32)[0] or 9
    out = []
    for i in range(nseg):
        sector, ln, flags, alloc = struct.unpack_from("<HHHH", d, ne + so + i * 8)
        if not ln:
            ln = 0x10000
        out.append({"sector": sector, "len": ln, "flags": flags,
                    "file": sector << sh})
    return out, sh


def relocated(d):
    """The module laid out one segment to a 64K window with its internal
    fixups applied, which is the only form its pointers mean anything in."""
    segs, sh = segments(d)
    ne = struct.unpack_from("<I", d, 0x3C)[0]
    body = {}
    for i, s in enumerate(segs):
        b = bytearray(s["len"])
        if s["sector"]:
            b[:] = d[s["file"]:s["file"] + s["len"]]
        body[i + 1] = b
    for i, s in enumerate(segs):
        if not (s["flags"] & 0x100) or not s["sector"]:
            continue
        p = s["file"] + s["len"]
        n = struct.unpack_from("<H", d, p)[0]
        b = body[i + 1]
        for k in range(n):
            at, rt, off, a, bb = struct.unpack_from("<BBHHH", d, p + 2 + k * 8)
            at &= 0x0F
            if rt & 3 or a == 0xFF or not 1 <= a <= len(segs):
                continue
            val = (a << 16) | bb
            cur = off
            for _ in range(4096):
                if cur == 0xFFFF or cur + 2 > len(b):
                    break
                nxt = struct.unpack_from("<H", b, cur)[0]
                if at == 2:
                    struct.pack_into("<H", b, cur, val >> 16)
                elif at == 3:
                    struct.pack_into("<H", b, cur, val & 0xFFFF)
                    if cur + 4 <= len(b):
                        struct.pack_into("<H", b, cur + 2, val >> 16)
                elif at == 0:
                    b[cur] = val & 0xFF
                else:
                    struct.pack_into("<H", b, cur, val & 0xFFFF)
                if rt & 4 or at == 0:
                    break
                cur = nxt
    return segs, body


def buckets(code):
    """The fifteen dictionary buckets, read out of the switch that selects
    them: each case loads an index offset and a segment, then a data offset
    and the same segment again."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    hits = []
    i = 0
    while i < len(code) - 3:
        # mov ax, imm16 ; mov cx, imm16 where the second is a segment number
        if code[i] == 0xB8 and i + 6 <= len(code) and code[i + 3] == 0xB9:
            a = struct.unpack_from("<H", code, i + 1)[0]
            c = struct.unpack_from("<H", code, i + 4)[0]
            if 2 <= c <= 40:
                data = None
                for ins in md.disasm(code[i + 6:i + 6 + 16], i + 6):
                    if ins.mnemonic == "mov" and ins.op_str.startswith("ax, "):
                        data = int(ins.op_str.split(", ")[1], 0)
                        break
                    if ins.mnemonic in ("jmp", "ret", "retf"):
                        break
                hits.append((i, c, a, 0 if data is None else data))
                i += 6
                continue
        i += 1
    runs = []
    cur = []
    for h in hits:
        if cur and h[0] - cur[-1][0] > 40:
            if len(cur) >= 12:
                runs.append(cur)
            cur = []
        cur.append(h)
    if len(cur) >= 12:
        runs.append(cur)
    return runs[0] if runs else []


def main():
    if len(sys.argv) < 3:
        print("usage: mkmap.py REFERENCE MODULE...", file=sys.stderr)
        return 2
    ref = open(sys.argv[1], "rb").read()
    for path in sys.argv[2:]:
        d = open(path, "rb").read()
        segs, body = relocated(d)
        n = len(segs)
        dg, diph = n, n - 1
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper()
        print("/* %s: %d segments, data segment %d, diphones %d */" % (name, n, dg, diph))

        secs = L.sections_ne(d)
        found = {}
        for block, cands in ANCHORS.items():
            for tab, va, ln in cands:
                start = L.RDATA_OFF + (va - L.RDATA_VA)
                if tab in FINE:
                    base, score = L.locate(ref, d, start, ln,
                                           window=8, step=2, minvotes=2)
                    need = ln * 6
                else:
                    base, score = L.locate(ref, d, start, ln)
                    need = ln * 8
                if base is None or score * 10 < need:
                    continue
                addr = L.to_addr(secs, base)
                if addr is None or (addr >> 16) != dg:
                    continue
                found[block] = (addr & 0xFFFF) - BLOCKS[tab][1]
                break
            if block not in found:
                print("    /* no anchor matched for the %s block */" % block)

        out = {}
        for tab, (anchor, eng_off) in BLOCKS.items():
            if anchor not in found:
                continue
            out[tab] = (dg << 16) | ((eng_off + found[anchor]) & 0xFFFF)

        # A phoneme attribute table starts with a zero; nudge if the block
        # offset landed one byte late.
        for tab in ("phattr1", "phattr2"):
            if tab not in out:
                continue
            off = out[tab] & 0xFFFF
            b = body[dg]
            for adj in (0, -1, 1):
                if 0 <= off + adj < len(b) and b[off + adj] == 0:
                    out[tab] = (dg << 16) | (off + adj)
                    break

        out["patterns"] = 2 << 16
        out["rules"] = 3 << 16
        out["outputs"] = 4 << 16
        out["dispatch"] = 5 << 16
        out["lts_index"] = 6 << 16
        out["trie_desc"] = 7 << 16
        out["voices"] = diph << 16

        # The diphone segment holds the voices' targets, then the records,
        # then the index, which runs to the end of the segment. Both boundaries
        # are found by trying them: an index base and a whole number of voices
        # are right when the entries they imply decode as records, and wrong
        # almost always.
        reclen = {0: 1, 1: 3, 2: 1, 3: 3, 4: 3, 5: 5, 6: 1, 7: 1}

        def decodes(db, recs, entry):
            p = recs + entry
            for _ in range(24):
                if p >= len(db):
                    return False
                b = db[p]
                p += reclen[(b & 0x70) >> 4]
                if b & 0x80:
                    return p <= len(db)
            return False

        # Forty-nine possible previous sounds by forty-eight current ones,
        # plus the empty slot: the index is prev * 48 + cur and both run to
        # 0x30, so the table is 2353 entries and ends with the segment. That
        # is tried first, and the search is only a fallback for a build whose
        # inventory is a different size.
        best = (None, None, -1, None, 0)
        for cand in range(max(7, n - 6), n):
            db = body[cand]
            if len(db) < 0x3000:
                continue
            for count in [2353] + list(range(2200, 2500)):
                off_base = (len(db) - count * 2) & ~1
                if off_base < 0x1004:
                    continue
                entries = [struct.unpack_from("<H", db, off_base + k * 2)[0]
                           for k in range(count)]
                live = [e for e in entries if 0 < e < off_base]
                if len(live) < 100:
                    continue
                for nv in range(1, 9):
                    recs = nv * 0x1004
                    if recs + max(live) >= off_base:
                        continue
                    ok = sum(1 for e in live[:200] if decodes(db, recs, e))
                    # Among bases that all decode, the one with the most live
                    # entries is the whole table rather than a tail of it.
                    key = (ok, len(live))
                    if key > (best[2], best[4]):
                        best = (off_base, recs, ok, cand, len(live))
                if best[2] >= 200 and count == 2353 and best[3] == cand:
                    break
        if best[0] is not None and best[2] > 100:
            diph = best[3]
            out["voices"] = diph << 16
            out["diph_offsets"] = (diph << 16) | best[0]
            out["diph_offsets_end"] = (diph << 16) | min(0xFFFF, len(body[diph]))
            out["diph_records"] = (diph << 16) | best[1]
        else:
            print("    /* the diphone inventory did not settle */")

        rr = L.ref_tok(ref)
        base, got = L.find_tok(rr, d, L.ROWFMT_16)
        if base is not None:
            addr = L.to_addr(secs, base)
            out["tokstates"] = addr
            tgt = L.tok_table(d, base, L.ROWFMT_16, 0, segs[0]["len"])
            hmap = L.map_handlers(rr, tgt)
            named = {v: k for k, v in L.HANDLERS.items()}
            out["_handlers"] = {named[h]: hmap[h][0] for h in hmap if h in named}

        bk = buckets(bytes(body[1]))
        if len(bk) >= 15:
            out["_buckets"] = [((c << 16) | a, (c << 16) | dd) for _, c, a, dd in bk[:15]]

        for k in sorted(out):
            if k.startswith("_"):
                continue
            print("    .%-14s = 0x%08x," % (k, out[k]))
        print("    .code_lo = 0x00000000, .code_hi = 0x%08x," % segs[0]["len"])
        print("    .tok_stride = 6, .tok_state_off = 0, .tok_state_w = 1,")
        print("    .tok_handler_off = 1, .tok_handler_w = 2,")
        print("    .tok_next_off = 5, .tok_next_w = 1,")
        print("    .lts_index_stride = 3, .rule_stride = 7, .rule_prio_w = 1,")
        print("    .trie_st_off = 1, .trie_li_off = 5, .trie_base_off = 9,")
        print("    .trie_max_off = 11, .trie_po_off = 13,")
        print("    .silence_f0 = 0xD1, .unvoiced_dur = 0x6E, .unvoiced_reps = 8,")
        print("    .vowel_dur_shift = 1, .trn_round = 0,")
        if "modmap" in found:
            print("    .s = {")
            for nm in sorted(STRINGS):
                print("        [BST_S_%s] = 0x%08x," %
                      (nm, (dg << 16) | ((STRINGS[nm] + found["modmap"]) & 0xFFFF)))
            print("    },")
        if "_buckets" in out:
            print("    .bucket_index = { %s }," %
                  " ".join("0x%08x," % b[0] for b in out["_buckets"]))
            print("    .bucket_data  = { %s }," %
                  " ".join("0x%08x," % b[1] for b in out["_buckets"]))
        else:
            print("    /* the dictionary switch was not recognised */")
        if "_handlers" in out:
            print("    .h = {")
            for h in sorted(out["_handlers"]):
                print("        [BST_H_%s] = 0x%08x," % (h, out["_handlers"][h]))
            print("    },")
        missing = [t for t in list(BLOCKS) + ["tokstates"] if t not in out]
        if missing:
            print("    /* not placed: %s */" % " ".join(missing))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
