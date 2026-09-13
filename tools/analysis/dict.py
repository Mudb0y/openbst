#!/usr/bin/env python3
"""Decodes the BeSTspeech pronunciation dictionary.

The dictionary is a sorted, front-coded, nibble-packed word list split into
fifteen buckets. A word is first rewritten as a stream of 4-bit codes, one to
three nibbles per letter, using one code table for the initial letter and
another for the rest, with a trailing silent 'e' stripped and recorded as a
flag. The first nibble of that stream picks the bucket; within a bucket a
binary search over an offset index locates the entry.

Each entry begins with its own length and the number of nibbles it shares with
the entry before it, so only the differing tail is stored. The remainder is the
pronunciation, again as nibbles, decoded through a table of single phonemes and
a table of common phoneme pairs.
"""

import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800

CHARMAP = 0x10021320
CODE_MEDIAL = 0x10020EA0
CODE_INITIAL = 0x10020F10
PH_SINGLE = 0x10092D68
PH_SINGLE_MAX = 0x10092E14
PH_PAIR = 0x10092E18
PH_PAIR_MAX = 0x10092F50

# index table, data base
BUCKETS = [
    (0x100432D0, 0x10037728), (0x10047AE0, 0x10043570), (0x1004C970, 0x10047BE8),
    (0x10050090, 0x1004CA90), (0x10055BA0, 0x10050160), (0x1005D4A0, 0x10055CF8),
    (0x100612A8, 0x1005D668), (0x10065AC8, 0x10061398), (0x100697D0, 0x10065BD8),
    (0x1006E008, 0x100698B0), (0x10074500, 0x1006E108), (0x1007C9C0, 0x10074678),
    (0x100857F0, 0x1007CBC0), (0x10088A80, 0x10085A08), (0x10092B20, 0x10088B40),
]


class Image:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def off(self, va):
        return RDATA_OFF + (va - RDATA_VA)

    def u8(self, va):
        return self.d[self.off(va)]

    def u16(self, va):
        return struct.unpack_from("<H", self.d, self.off(va))[0]

    def s16(self, va):
        return struct.unpack_from("<h", self.d, self.off(va))[0]


def encode_word(img, word):
    """Mirrors the engine's word encoder: nibbles, built backwards then reversed."""
    w = word.lower()
    flag = 0
    if w.endswith("e"):
        w = w[:-1]
        flag = 1

    nib = []
    for i in range(len(w) - 1, -1, -1):
        idx = img.u8(CHARMAP + ord(w[i])) - 1
        if idx > 0x34:
            idx = 0x34
        table = CODE_INITIAL if i == 0 else CODE_MEDIAL
        v = img.u16(table + idx * 2)
        nib.append(v & 0xF)
        if v > 0xF:
            nib.append((v >> 4) & 0xF)
            if v > 0xFF:
                nib.append((v >> 8) & 0xF)

    # The engine builds [flag, nibbles...] while walking the word backwards,
    # then reverses the lot, so the count leads and the silent-e flag trails.
    buf = [flag] + nib
    out = [len(buf)] + list(reversed(buf))
    if len(out) > 2 and out[1] == 0x0F and out[2] < 8:
        out[1] = 1
    return out


def nib_at(img, base, i):
    """Nibble i of the entry at byte address base."""
    b = img.u8(base + (i >> 1))
    return b & 0xF if i & 1 else b >> 4


def entry_header(img, base):
    """Returns (total nibbles in the entry, shared prefix length, header size).

    Mirrors the engine's decoder exactly, including that the shared-prefix
    field is read from whichever nibble follows the length field.
    """
    b = img.u8(base)
    if (b & 0xF0) == 0xF0:
        if b == 0xFF:
            total, used, p = (img.u8(base + 1) >> 4) + 0x20, 3, base + 1
        else:
            total, used, p = (b & 0xF) + 0x11, 2, base + 1
    else:
        total, used, p = (b >> 4) + 2, 1, base

    if used & 1:
        if (img.u8(p) & 0xF) == 0xF:
            shared, used = (img.u8(p + 1) >> 4) + 0xF, used + 2
        else:
            shared, used = img.u8(p) & 0xF, used + 1
    else:
        v = img.u8(p)
        if (v & 0xF0) == 0xF0:
            shared, used = (v & 0xF) + 0xF, used + 2
        else:
            shared, used = v >> 4, used + 1
    return total, shared, used


def compare(img, word, base, state):
    """The engine's entry comparison. Returns (result, new state).

    0 means this entry is the word. A positive value is the entry's length,
    meaning step past it. -1 means the search has gone too far.
    """
    total, shared, used = entry_header(img, base)
    if shared < state[0]:
        return -1, state
    if state[0] < shared:
        return total, state

    i = shared + 1
    n = used
    last = total
    while i <= word[0]:
        v = nib_at(img, base, n)
        last = v
        if word[i] != v:
            break
        i += 1
        n += 1
    state[0] = i - 1
    if i != word[0] + 1:
        return (-1 if word[i] <= last else total), state
    state[0] = n
    return 0, state


def lookup(img, word):
    """Finds a word and returns the nibble offset of its pronunciation."""
    enc = encode_word(img, word)
    b = enc[1] - 1 if len(enc) > 1 else -1
    if not 0 <= b < len(BUCKETS):
        return None
    idx_va, data_va = BUCKETS[b]
    count = img.u16(idx_va)
    if count < 2:
        return None

    lo, hi = 1, count - 1
    state = [0]
    entry = None
    res = None
    mid = 1
    while lo <= hi:
        mid = (lo + hi) >> 1
        state = [0]
        entry = data_va + img.u16(idx_va + mid * 2)
        res, state = compare(img, enc, entry, state)
        if res == 0:
            return entry, state[0]
        if res < 0:
            hi = mid - 1
        else:
            lo = mid + 1

    if res is not None and res < 0:
        if mid == 1:
            return None
        mid -= 1

    limit = img.u16(idx_va + (mid + 1) * 2)
    entry = data_va + img.u16(idx_va + mid * 2)
    state = [0]
    while entry < data_va + limit:
        res, state = compare(img, enc, entry, state)
        if res <= 0:
            break
        entry += res
    if res != 0:
        return None
    return entry, state[0]


def main():
    if len(sys.argv) < 3:
        print("usage: dict.py DLL WORD...", file=sys.stderr)
        return 2
    img = Image(sys.argv[1])

    for word in sys.argv[2:]:
        enc = encode_word(img, word)
        r = lookup(img, word)
        if r is None:
            print("%-14s encoded %-24s NOT FOUND"
                  % (word, " ".join("%x" % n for n in enc)))
        else:
            entry, pos = r
            total, shared, used = entry_header(img, entry)
            print("%-14s encoded %-24s found at 0x%08x, %d nibbles, pron from nibble %d"
                  % (word, " ".join("%x" % n for n in enc), entry, total, pos))
    return 0


if __name__ == "__main__":
    sys.exit(main())
