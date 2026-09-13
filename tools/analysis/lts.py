#!/usr/bin/env python3
"""Letter-to-sound rule engine.

Words the dictionary does not hold are pronounced by a context-sensitive
rewrite system in the NRL style. At each position the engine tries a two-letter
key then a one-letter key, hashes it modulo 237 into a dispatch table, and
walks a chain of candidate rules. A rule fires when its right context matches
forwards from the end of the focus and its left context matches backwards from
the start.

Context classes, all of which appear in the stored patterns:
    space  a word boundary (a non-letter)
    #      a vowel
    ^      one consonant
    :      zero or more consonants
    +      a front vowel, meaning i, e or y
    %      one of a list of stored suffixes
    .      a voiced consonant
    &      a sibilant, allowing a doubled letter
    @      h after t, c or s, else a letter with its own attribute bit
"""

import struct
import sys

RDATA_VA, RDATA_OFF = 0x10020000, 0x17800
DATA_VA, DATA_OFF = 0x10096000, 0x8CC00

IS_LETTER = 0x10021020     # bit 3 marks a letter
LETTER_ATTR = 0x10021120   # 1 vowel, 2 voiced, 4 consonant, 0x10 for '@'
SUFFIX_PTRS = 0x10020E90
INDEX = 0x1002E818
DISPATCH = 0x10022ED0
RULES = 0x10093000
PATTERNS = 0x1002EFB8
OUTPUTS = 0x10021C80
PH_ATTR1 = 0x10021648   # bit 0x80 opens a new group
PH_ATTR2 = 0x100216C8   # bits 2 and 4 choose which slot a code lands in


class Img:
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

    def cstr(self, va):
        p = self.off(va)
        e = self.d.index(b"\0", p)
        return self.d[p:e].decode("latin1")

    def ptr(self, va):
        return struct.unpack_from("<I", self.d, self.off(va))[0]

    def dstr(self, va):
        """A string in .data, which is mapped separately from .rdata."""
        p = DATA_OFF + (va - DATA_VA)
        e = self.d.index(b"\0", p)
        return self.d[p:e].decode("latin1")


class Lts:
    def __init__(self, img):
        self.img = img
        # The three suffixes the "%" class recognises. They live in .data, not
        # .rdata, which is why a naive offset lookup reads rubbish.
        self.suffixes = [img.dstr(img.ptr(SUFFIX_PTRS + i * 4)) for i in range(3)]

    def is_letter(self, c):
        return bool(self.img.u8(IS_LETTER + c) & 8)

    def attr(self, c):
        return self.img.u8(LETTER_ATTR + c)

    def match(self, pattern, pi, text, ti, step):
        """Walks one side of a pattern. pi indexes the pattern, ti the text,
        and step is +1 for the right context and -1 for the left."""
        while 0 <= pi < len(pattern) and pattern[pi] != "\0":
            pc = pattern[pi]
            if pc == ")":
                pi += 1
                continue
            tc = text[ti] if 0 <= ti < len(text) else 0

            if self.is_letter(ord(pc)) or pc == "`":
                if ord(pc) == tc:
                    pi += step
                    ti += step
                    continue
                return False
            if pc == " ":
                if tc and self.is_letter(tc):
                    return False
                pi += step
                ti += step
                continue
            if pc == "#":
                if not (self.attr(tc) & 1):
                    return False
                pi += step
                ti += step
                continue
            if pc == "^":
                if not (tc and self.is_letter(tc) and (self.attr(tc) & 4)):
                    return False
                pi += step
                ti += step
                continue
            if pc == ":":
                while tc and self.is_letter(tc) and (self.attr(tc) & 4):
                    ti += step
                    tc = text[ti] if 0 <= ti < len(text) else 0
                pi += step
                continue
            if pc == "+":
                if tc not in (ord("i"), ord("e"), ord("y")):
                    return False
                pi += step
                ti += step
                continue
            if pc == ".":
                if not (tc and self.is_letter(tc) and (self.attr(tc) & 2)):
                    return False
                pi += step
                ti += step
                continue
            if pc == "%":
                # Collect up to three consecutive letters and require an exact
                # match against one of the suffixes, not a prefix match.
                got = ""
                k = ti
                while len(got) < 3 and 0 <= k < len(text) and self.is_letter(text[k]):
                    got += chr(text[k])
                    k += 1
                if got not in self.suffixes:
                    return False
                ti += len(got) - 1
                pi += step
                continue
            if pc == "@":
                if tc == ord("h"):
                    prev = text[ti - 1] if ti > 0 else 0
                    if prev not in (ord("t"), ord("c"), ord("s")):
                        return False
                    pi -= 1
                    ti -= 2
                    continue
                if not (tc and self.is_letter(tc) and (self.attr(tc) & 0x10)):
                    return False
                pi -= 1
                ti -= 1
                continue
            return False
        return True

    def rule_chain(self, key):
        bucket = key % 0xED
        o = self.img.off(INDEX) + bucket * 4
        entry_off = struct.unpack_from("<h", self.img.d, o)[0]
        count = self.img.d[o + 2]
        if entry_off < 0:
            return None
        for k in range(count):
            p = DISPATCH + entry_off * 4 + k * 4
            rkey = self.img.s16(p)
            ridx = self.img.s16(p + 2)
            if rkey == key:
                return ridx
            if rkey < key:
                break
        return None

    def try_at(self, text, pos):
        """Returns (rule index, letters consumed) for the rule that fires.

        The pattern's focus holds only the letters beyond the dispatch key, so
        the forward walk starts at the text position just past the key and the
        focus characters are matched as ordinary literals on the way.

        Rules carry a priority, lower being better. Both key lengths are tried
        and the best priority wins, so a one-letter rule can beat a two-letter
        one. Within a chain the walk stops at the first rule whose priority is
        no better than the best match so far, rather than skipping past it.
        """
        best = None
        threshold = 0x240

        for span in (2, 1):
            if pos + span > len(text):
                continue
            if span == 2 and not (self.is_letter(text[pos + 1]) or text[pos + 1] == 0x60):
                continue
            key = text[pos] if span == 1 else text[pos] | (text[pos + 1] << 8)
            ridx = self.rule_chain(key)

            while ridx is not None and ridx >= 0:
                prio, nxt, pat, _ = struct.unpack_from(
                    "<hhhh", self.img.d, self.img.off(RULES) + ridx * 8)
                if (prio & 0xFF) >= threshold:
                    break
                pattern = self.img.cstr(PATTERNS + pat)
                t = pattern.index("T")
                focus = pattern[t + 1:pattern.index(")")]

                if (self.match(pattern, t + 1, text, pos + span, 1) and
                        self.match(pattern, t - 1, text, pos - 1, -1)):
                    best = (ridx, span + len(focus))
                    threshold = prio & 0xFF
                    break
                ridx = nxt if nxt >= 0 else None

        return best if best else (None, 1)

    def pronounce(self, word, prenormalised=False):
        # The engine works on a buffer delimited by underscores, and counts
        # positions from the first real letter. Normalisation happens upstream
        # and can strip a plural 's' or a final 'e', so a caller that already
        # has the engine's buffer passes it through untouched.
        if prenormalised:
            text = word.encode("latin1")
        else:
            text = b"_" + word.lower().encode("latin1") + b"__"
        out = []
        pos = 1
        # The driver stops at the closing delimiter, not at the buffer end.
        while pos < len(text) and text[pos] not in (ord("_"), 0):
            ridx, used = self.try_at(text, pos)
            if ridx is None:
                pos += 1
                continue
            _, _, _, o = struct.unpack_from(
                "<hhhh", self.img.d, self.img.off(RULES) + ridx * 8)
            out.append((ridx, OUTPUTS + o))
            pos += max(used, 1)
        return out


class Builder:
    """Turns a stream of phoneme codes into the engine's record buffer.

    A code that opens a group takes three slots: one before it, itself, and
    one after. Codes that do not open a group are written into whichever of
    those slots their attributes select, which is how stress and allophone
    markers attach to the segment they modify.
    """

    LIMIT = 0x60

    def __init__(self, img):
        self.img = img
        self.buf = bytearray(128)
        self.pos = 0
        self.before = -1
        self.at = -1
        self.after = -1
        self.stopped = False

    def emit(self, code):
        if self.stopped:
            return
        p = self.pos
        if p >= self.LIMIT:
            self.stopped = True
            return

        a1 = self.img.u8(PH_ATTR1 + code)
        a2 = self.img.u8(PH_ATTR2 + code)
        opens = (a1 & 0x80) or code == 0x4C or code == 0x49

        if not opens:
            special = 0x75 < code < 0x7C
            if (a2 & 2) == 0 and not special:
                if (a2 & 4) == 0:
                    p = self.pos
                    self.pos = p + 1
                    self.before = self.at = self.after = -1
                elif self.at == -1:
                    if self.after == -1:
                        return
                    p = self.after
                    self.after = -1
                else:
                    p = self.at
                    self.before = self.at = -1
            else:
                p = self.before
                if p == -1:
                    return
                self.before = -1
        else:
            self.before = p + 1
            self.at = p + 2
            self.after = p + 3
            self.pos = p + 4
            for k in (3, 4, 5):
                if p + k < len(self.buf):
                    self.buf[p + k] = 0

        if 0 <= p + 2 < len(self.buf):
            self.buf[p + 2] = code

    def result(self):
        return bytes([self.pos & 0xFF]) + bytes(self.buf[1:self.pos + 2])


def main():
    if len(sys.argv) < 3:
        print("usage: lts.py DLL WORD...", file=sys.stderr)
        return 2
    img = Img(sys.argv[1])
    lts = Lts(img)
    args = sys.argv[2:]
    pre = False
    if args and args[0] == "--buf":
        pre = True
        args = args[1:]
    for w in args:
        fired = lts.pronounce(w, pre)
        b = Builder(img)
        for _, addr in fired:
            for c in img.cstr(addr).encode("latin1"):
                b.emit(c)
        print("%-12s rules %-44s buffer %s"
              % (w, " ".join("r%d" % i for i, _ in fired),
                 " ".join("%02X" % c for c in b.result())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
