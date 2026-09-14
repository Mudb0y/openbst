#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* Word stress: choosing which syllable of a word carries the accent.
 *
 * The word arrives as a code stream in which every syllable opener is followed
 * by three slots, and this fills the first of them. The dictionary and the
 * rules can both mark a syllable already -- a strong mark, a weak one, or one
 * of two that say "not this one" -- and what is left is decided here: with no
 * marks at all a one-syllable word takes the stress, and a longer word takes
 * it on the last syllable but two, skipped forward over anything that refuses.
 * Every syllable that ends up without a mark gets the reduced one. */

#define SYL_MAX 26

#define F_LONG   0x02   /* the opener's own attributes refuse the stress */
#define F_MARKED 0x10   /* this syllable has the accent */
#define F_WEAK   0x20   /* it was marked weak, so it cannot take the accent */
#define F_NEXT   0x01   /* a neighbour of the accent */
#define F_VOWEL  0x08   /* the word ends on a vowel */

static int a1(const bst_image *img, int c) { return bst_ph_attr1(img, c); }
static int a2(const bst_image *img, int c) { return bst_ph_attr2(img, c); }

/* What an opener's own code says about taking the stress. */
static int syl_flags(int c) {
    if (c == '$' || c == '%') return 6;
    if (c == '.' || c == ')' || c == '#') return 4;
    return 0;
}

static int is_mark(int c) { return c > 0x75 && c < 0x7C; }

/* Writes the accent mark, in the form the emphasis flags select. */
static void mark(uint8_t *p, int emph) {
    if (!(emph & 1)) *p = (emph & 4) ? 0x37 : 0x36;
    else             *p = (emph & 4) ? 0x32 : 0x35;
}

/* The Romance rule: one syllable takes the accent and the rest are flat.
   Which one is the last but one, unless the word ends in a consonant the
   build does not except, in which case it is the last. Rule four is French,
   which has no word accent to place and marks every syllable alike. */
static void romance(const bst_image *img, uint8_t *s, int len, int emph) {
    int16_t slot[SYL_MAX + 4];
    int n = 0;

    for (int i = 0; i < len; i++) {
        int c = s[i];
        if (a2(img, c) & 0x10) continue;
        if (!(a1(img, c) & 0x80) || n + 2 >= SYL_MAX) continue;
        slot[++n] = (int16_t)(i + 1);
        i += 3;
    }
    if (n == 0) return;

    int fin = 0;
    for (int i = len - 1; i >= 0; i--)
        if (s[i] && s[i] < 0x2F) { fin = s[i]; break; }
    int keep = img->t.stress_rule == 3 || (a1(img, fin) & 0x80) != 0;
    for (int k = 0; !keep && k < (int)sizeof img->t.stress_keep; k++)
        if (img->t.stress_keep[k] && fin == img->t.stress_keep[k]) keep = 1;

    int at = keep && n > 1 ? n - 1 : n;
    int hi = (emph & 1) && img->t.stress_rule != 4
                 ? (img->t.stress_rule == 2 ? 0x35 : 0x33) : 0x36;
    int lo = (emph & 1) ? (img->t.stress_rule == 2 ? 0x33 : 0x31) : 0x32;
    for (int k = 1; k <= n; k++)
        s[slot[k]] = (uint8_t)(img->t.stress_rule == 4 || k == at ? hi : lo);
}


/* Russian reduces every vowel that does not carry the accent. Which grade it
   takes depends on how far the accent is and on what stands before it: a soft
   consonant pulls the reduction to the front pair. */
static void ru_reduce(const bst_image *img, uint8_t *s, int v, int before, int strong) {
    int c = s[v];
    if (c < 0x27 || c > 0x2B) return;
    int prev = v > 0 ? s[v - 1] : 0;
    if (c == 0x27 || c == 0x28) {
        if ((a1(img, prev) & 8) || prev == 0x26 || prev == 0x1C ||
            prev == 0x1B || prev == 0x1D)
            s[v] = (uint8_t)bst_uncode(img, 0x2F);
        return;
    }
    if (c == 0x2A) return;
    if (c == 0x29) {
        if (before && (a1(img, prev) & 8)) {
            s[v] = (uint8_t)bst_uncode(img, 0x2F);
            return;
        }
        if (prev == 0x1E) {
            int nx = s[v + 1];
            if (nx != bst_uncode(img, 0x33) && nx != 0) {
                s[v] = (uint8_t)bst_uncode(img, 0x2F);
                return;
            }
        }
    }
    s[v] = (uint8_t)bst_uncode(img, strong == 2 ? 0x31 : 0x32);
}

void bst_word_stress(const bst_image *img, uint8_t *s, int len, int emph, int mode) {
    uint8_t flags[SYL_MAX + 4];
    int16_t slot[SYL_MAX + 4];
    int n = 0, first = 1, chosen = 0, tail = 0;
    int soft = 0, unstressed = 1;

    memset(flags, 0, sizeof flags);
    memset(slot, 0, sizeof slot);
    flags[1] = 0;

    if (img->t.stress_rule) {
        for (int i = 0; i < len; i++) {
            int c = s[i];
            if (!(a2(img, c) & 0x10)) continue;
            if (c == 0x4F || c == 0x50) { if (!(emph & 2)) emph |= 5; }
            else if (c == 0x51)         { if (!(emph & 2)) emph |= 1; }
            else if (c == 0x52)         { if (!(emph & 1)) emph |= 6; }
        }
        romance(img, s, len, emph);
        return;
    }

    for (int i = 0; i < len; i++) {
        int c = s[i];
        if (a2(img, c) & 0x10) {
            if (c == 0x4F || c == 0x50) { if (!(emph & 2)) emph |= 5; }
            else if (c == 0x51)         { if (!(emph & 2)) emph |= 1; }
            else if (c == 0x52)         { if (!(emph & 1)) emph |= 6; }
            continue;
        }
        if (!(a1(img, c) & 0x80) || n > 0x18) {
            int prev;
            if (c == 0x49) {
                tail = n;
                s[i] = 0;
                prev = a1(img, i > 0 ? s[i - 1] : 0);
            } else if (c == 0x4C) {
                prev = a1(img, i > 0 ? s[i - 1] : 0);
            } else continue;
            if (prev & 1) flags[n] |= F_VOWEL;
            continue;
        }

        if (n + 2 >= SYL_MAX) break;
        n++;
        int m = i + 1;
        flags[n + 1] = 0;
        slot[n] = (int16_t)m;
        flags[n] |= (uint8_t)syl_flags(c);
        int v = s[m];
        if (is_mark(v)) {
            soft = 1;
            i += 3;
            continue;
        }
        if (v == 0x36) {
            if (emph & 1) s[m] = 0x35;
            flags[n] |= F_MARKED;
            flags[n - 1] |= F_NEXT;
            flags[n + 1] |= F_NEXT;
            chosen = n;
            i += 3;
        } else if (v == 0x35) {
            if (emph & 1) s[m] = 0x33;
            flags[n] |= F_WEAK;
            unstressed = 0;
            i += 3;
        } else {
            if (v == 0x37 || v == 0x38) {
                flags[n] |= F_MARKED;
                flags[n - 1] |= F_NEXT;
                flags[n + 1] |= F_NEXT;
                chosen = n;
            }
            i += 3;
        }
    }

    if ((a1(img, len > 0 ? s[len - 1] : 0) & 1) && tail == 0) flags[n] |= F_VOWEL;
    tail = n;

    if (chosen == 0 && n != 0) {
        if (n == 1) {
            unstressed = 0;
            if (!(a2(img, s[slot[1]]) & 2)) mark(&s[slot[1]], emph);
        } else {
            int last = n;
            if (n > 1) {
                while (last > 1 && s[slot[last]] == 0x76) last--;
                if (last > 1)
                    while (first < last && s[slot[first]] == 0x76) first++;
            }
            chosen = 0;

            /* A word whose syllables carry the "not this one" marks is walked
               backwards, and the first syllable two clear of the last refusal
               takes the accent. */
            if (soft) {
                int gap = 4;
                for (int k = last; k >= first; k--) {
                    int v = s[slot[k]];
                    if (v == 0x76) {
                        if (gap == 2) gap = 1;
                    } else if (v == 0x77) {
                        if (!(flags[k] & (F_LONG | F_WEAK))) {
                            mark(&s[slot[k]], emph);
                            gap = 4;
                            flags[k] |= F_MARKED;
                            flags[k - 1] |= F_NEXT;
                            flags[k + 1] |= F_NEXT;
                            chosen = k;
                        }
                    } else if (v == 0x78) {
                        gap = 1;
                    } else if (v == 0x79) {
                        gap = 0;
                    } else if (v == 0x7A) {
                        if (unstressed && !(flags[k] & F_LONG)) {
                            s[slot[k]] = (emph & 1) ? 0x33 : 0x35;
                        }
                        gap = 0;
                        last = k;
                    } else if (v == 0x7B) {
                        s[slot[k]] = 0x33;
                        gap = 0;
                        last = k;
                    } else if (gap == 2 && !(flags[k] & (F_LONG | F_WEAK))) {
                        mark(&s[slot[k]], emph);
                        flags[k] |= F_MARKED;
                        flags[k - 1] |= F_NEXT;
                        flags[k + 1] |= F_NEXT;
                        chosen = k;
                    }
                    gap++;
                }
            }

            if (chosen == 0 && img->t.stress_kind == 2) {
                /* Japanese chooses nothing: what the rules marked is all the
                   accent a word gets. */
            } else if (chosen == 0 && img->t.stress_kind == 4) {
                /* Arabic: the accent goes on the last long vowel, and on the
                   first syllable if the word has none. */
                int k = 0;
                for (int j = last; j >= first; j--)
                    if (a1(img, s[slot[j] - 1]) & 8) { k = j; break; }
                if (k == 0) k = first;
                flags[k] |= F_MARKED;
                mark(&s[slot[k]], emph);
                chosen = k;
            } else if (chosen == 0 && img->t.stress_kind == 1) {
                /* Hebrew has no search: what is left unchosen takes the
                   accent on its last syllable. */
                s[slot[last]] = 0x36;
            } else if (chosen == 0) {
                int k = first;
                if (first + 2 < last) {
                    k = last - 2;
                    while (k > first && ((flags[k] & F_LONG) || s[slot[k]] == 0x76)) k--;
                }
                while (k < n && ((flags[k] & F_LONG) || (a2(img, s[slot[k]]) & 2))) k++;
                if (!(flags[k] & F_LONG) && !(a2(img, s[slot[k]]) & 2)) {
                    flags[k] |= F_MARKED;
                    flags[k - 1] |= F_NEXT;
                    flags[k + 1] |= F_NEXT;
                    mark(&s[slot[k]], emph);
                }
            }
        }
    }

    if (bst_trace) {
        fprintf(stderr, "stress n=%d chosen=%d emph=%d unstressed=%d tail=%d flags",
                n, chosen, emph, unstressed, tail);
        for (int k = 1; k <= tail; k++) fprintf(stderr, " %02x", flags[k]);
        fprintf(stderr, " marks");
        for (int k = 1; k <= tail; k++) fprintf(stderr, " %02x", s[slot[k]]);
        fprintf(stderr, "\n");
    }

    /* Russian: the accent takes the strong mark, the syllable just before it
       a middle one, and the rest the weak one, with the vowel reduced to the
       grade its distance from the accent allows. */
    if (img->t.stress_kind == 3) {
        int at = 0, before = 1;
        for (int k = 1; k <= tail; k++)
            if ((flags[k] & F_MARKED) || s[slot[k]] == 0x36 || s[slot[k]] == 0x37)
                at = k;
        for (int k = 1; k <= tail; k++) {
            int m = slot[k], v = m - 1;
            if (k == at) { before = 0; continue; }
            if (k == at - 1) {
                ru_reduce(img, s, v, before, 2);
                if (s[m] == 0 || is_mark(s[m])) s[m] = 0x33;
                continue;
            }
            int c = s[v], prev = v > 0 ? s[v - 1] : 0;
            if (c == 0x29 && k == tail && !(a1(img, prev) & 8))
                ru_reduce(img, s, v, before, 2);
            else if (c == 0x2C && k != tail && (a1(img, prev) & 8) &&
                     (a1(img, s[m + 3]) & 8))
                s[v] = 0x2D;
            else if (c == 0x28 && k == tail)
                s[v] = (uint8_t)bst_uncode(img, 0x30);
            else
                ru_reduce(img, s, v, before, 1);
            if (s[m] == 0 || is_mark(s[m])) s[m] = 0x32;
        }
        return;
    }

    /* Everything else reduces, and an empty slot becomes the reduced mark. */
    if (img->t.stress_kind == 1 || img->t.stress_kind == 2) {
        for (int k = tail; k > 0; k--) {
            if (s[slot[k]] == 0x35 && k < tail) s[slot[k + 1]] = 0x31;
            int v = s[slot[k]];
            if (v == 0 || is_mark(v)) s[slot[k]] = 0x32;
        }
        return;
    }
    for (int k = tail; k > 0; k--) {
        if (unstressed && k != 0) {
            int f = flags[k];
            if (!(f & (F_MARKED | F_LONG | 4)) && (!(f & F_NEXT) || (f & F_VOWEL)) &&
                (!(mode & 8) || s[slot[k]] == 0))
                s[slot[k]] = (emph & 1) ? 0x33 : 0x35;
        }
        int v = s[slot[k]];
        if (v == 0 || is_mark(v)) s[slot[k]] = 0x32;
    }
}
