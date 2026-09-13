#!/usr/bin/env python3
"""Word normalisation: the suffix stripper that runs before lookup.

Before either the dictionary or the letter-to-sound rules see a word, the
engine copies it into a buffer delimited by underscores, rewrites an apostrophe
as a backtick, counts its vowels, and then strips one inflectional suffix:
-ed, -ing, -s, -'s or -ly. Which suffix was removed is recorded in a flag byte
so a later stage can put the sound back.

The vowel count stands in for a syllable count and gates most of the rules,
which is why "toves" loses its s but "as" does not.
"""

import sys

VOWELS = set("aeiouy")


# Letter attributes come from the engine's own table rather than a guess at
# which letters double: bit 0 vowel, bit 1 voiced, bit 2 consonant, bit 3 a
# consonant that may double.
LETTER_ATTR_VA = 0x10021120
RDATA_VA, RDATA_OFF = 0x10020000, 0x17800


def load_attrs(dll):
    d = open(dll, "rb").read()
    base = RDATA_OFF + (LETTER_ATTR_VA - RDATA_VA)
    return {chr(c): d[base + c] for c in range(256)}


class Normaliser:
    ED, ING, LY, S, APOS = 0x80, 0x40, 0x20, 0x10, 0x08

    def __init__(self, dll):
        self.attrs = load_attrs(dll)
        self.flags = 0
        self.vowels = 0

    def attr(self, c):
        return self.attrs.get(c, 0)

    def _fa20(self, w, i):
        """Decides whether a stem ending here wants its silent e back."""
        c = w[i] if 0 <= i < len(w) else ""
        p = w[i - 1] if i >= 1 else ""
        if self.attr(c) & 4:
            return 0
        if not (self.attr(c) & 1) or not (self.attr(p) & 1):
            return 1
        if p == "u" and i >= 2 and w[i - 2] in ("q", "g"):
            return 1
        return 0

    def undouble(self, w, i):
        """The engine's stem check before -ed and -ing come off. Returns the new
        last index, or None to refuse the strip. A doubled consonant collapses;
        otherwise a long switch decides whether the stem stands as it is or
        wants a silent e restored, which is what turns "hop" back into "hope".
        """
        if i < 0 or i >= len(w):
            return None
        c = w[i]
        p = w[i - 1] if i >= 1 else ""
        if p == c and (self.attr(c) & 8):
            return i - 1

        add_e = False
        v = self.vowels

        if c in ("a", "e", "o", "x", "y"):
            pass
        elif c == "b":
            if p in ("a", "e", "i", "o", "u", "y"):
                add_e = True
            elif p in ("l", "m", "r"):
                pass
            else:
                return None
        elif c in ("c", "u", "v"):
            add_e = True
        elif c == "g":
            if not (p == "n" and i >= 2 and w[i - 2] in ("i", "o")):
                add_e = True
        elif c == "h":
            if p == "t":
                add_e = True
        elif c == "i":
            w[i] = "y"
        elif c == "l":
            if p in ("a", "e"):
                if not v > 2:
                    add_e = True
            elif p in "bcdfghjkmnpqstvxz":
                return None
            else:
                add_e = bool(self._fa20(w, i - 1))
        elif c == "r":
            if self.attr(p) & 4:
                return None
            if v > 2:
                if p == "a":
                    if (i >= 2 and w[i - 2] == "p") or \
                       (i >= 3 and w[i - 2] == "l" and w[i - 3] == "c"):
                        add_e = True
                    else:
                        pass
                elif p in ("e", "o"):
                    pass
                else:
                    add_e = bool(self._fa20(w, i - 1))
            else:
                add_e = True
        elif c == "s":
            if p == "s":
                pass
            elif p == "u" and i >= 2 and w[i - 2] == "o":
                return None
            else:
                add_e = True
        elif c == "t":
            if p == "a":
                if not (i >= 2 and w[i - 2] in ("o", "e")):
                    add_e = True
            elif p == "e":
                if not v > 2:
                    add_e = True
            elif p == "i":
                if v > 2 and (i < 2 or w[i - 2] != "v") and \
                   not (i >= 3 and w[i - 2] == "r" and w[i - 3] == "w"):
                    pass
                else:
                    add_e = True
            else:
                add_e = bool(self._fa20(w, i - 1))
        elif c == "w":
            if self.attr(p) & 4:
                return None
        elif c == "z":
            if self.attr(p) & 1:
                add_e = True
        else:
            if not self._fa20(w, i - 1):
                return i
            add_e = True
            if v > 2:
                if c == "m":
                    if p == "o" and (i < 2 or w[i - 2] != "c"):
                        add_e = False
                elif c == "n":
                    if p in ("a", "o"):
                        add_e = False
                    elif p == "e":
                        if not (i >= 3 and w[i - 2] == "v" and (self.attr(w[i - 3]) & 4)):
                            add_e = False
                elif c == "p" and p in ("i", "o"):
                    add_e = False

        if add_e:
            if i + 1 < len(w):
                w[i + 1] = "e"
            else:
                w.append("e")
            return i + 1
        return i

    def strip(self, word):
        w = list(word.lower().replace("'", "`"))
        self.vowels = sum(1 for c in w if c in VOWELS)
        self.flags = 0
        end = len(w) - 1          # index of the last character

        while True:
            again = False
            keep = end
            c = w[end] if end >= 0 else ""

            if c == "d":
                if end >= 1 and w[end - 1] == "e" and self.vowels > 1 and \
                        (end < 2 or w[end - 2] != "e"):
                    keep = self.undouble(w, end - 2)
                    if keep is not None:
                        self.flags |= self.ED
                        end = keep

            elif c == "g":
                if end >= 2 and w[end - 1] == "n" and w[end - 2] == "i" and self.vowels > 1:
                    keep = self.undouble(w, end - 3)
                    if keep is not None:
                        self.flags |= self.ING
                        end = keep

            elif c == "s":
                if self.vowels != 0:
                    prev = w[end - 1] if end >= 1 else ""
                    take = True
                    keep = end - 1
                    if prev == "`":
                        self.flags |= self.APOS
                        end = end - 2
                        again = True
                        take = False
                    elif prev in ("i", "s", "u"):
                        take = False
                    elif prev in ("a", "o", "y"):
                        if self.vowels == 1:
                            take = False
                    elif prev in ("d", "g"):
                        again = True
                    elif prev == "e":
                        if self.vowels < 2:
                            take = False
                        else:
                            p2 = w[end - 2] if end >= 2 else ""
                            p3 = w[end - 3] if end >= 3 else ""
                            if p2 == "h":
                                keep = end - 2 if p3 in ("s", "c") else end - 1
                            elif p2 == "i":
                                self.vowels -= 1
                                again = True
                                w[end - 2] = "y"
                                keep = end - 2
                            elif p2 in ("j", "o", "x"):
                                keep = end - 2
                            elif p2 == "s":
                                keep = end - 1
                                if p3 == "u" and self.vowels > 2:
                                    keep = end - 2
                                    if end >= 4 and w[end - 4] == "o":
                                        take = False
                            elif p2 == "z":
                                # Tests the consonant bit, not the whole byte.
                                keep = end - 1 if (self.attr(p3) & 4) == 0 else end - 2
                            else:
                                keep = end - 1
                    if take:
                        self.flags |= self.S
                        end = keep

            elif c == "y":
                if end >= 1 and w[end - 1] == "l" and self.vowels > 1:
                    keep = end - 2
                    p = w[end - 2] if end >= 2 else ""
                    take = True
                    if p in ("a", "b", "o", "u"):
                        take = False
                    elif p in ("d", "g"):
                        self.vowels -= 1
                        again = True
                    elif p == "e":
                        take = self.vowels >= 3
                    elif p == "i":
                        if self.vowels < 3 or (end >= 4 and w[end - 3] == "r"
                                               and w[end - 4] == "a"):
                            take = False
                        else:
                            w[end - 2] = "y"
                    elif p == "l":
                        if self.vowels < 3:
                            take = False
                        else:
                            b = w[end - 3] if end >= 3 else ""
                            if b == "a":
                                if end >= 5 and w[end - 4] == "c" and w[end - 5] == "i":
                                    keep = end - 4
                            elif b != "u" and self.attr(w[end - 4] if end >= 4 else "") == 0:
                                take = False
                    if take:
                        self.flags |= self.LY
                        end = keep

            if not again:
                break

        return "_" + "".join(w[:end + 1]) + "__", self.flags


def main():
    if len(sys.argv) < 3:
        print("usage: normalise.py DLL WORD...", file=sys.stderr)
        return 2
    n = Normaliser(sys.argv[1])
    for w in sys.argv[2:]:
        buf, flags = n.strip(w)
        print("%-16s %-16s flags 0x%02x" % (w, buf, flags))
    return 0


if __name__ == "__main__":
    sys.exit(main())
