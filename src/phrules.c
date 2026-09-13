#include <string.h>
#include "bst_text.h"

/* The phonological rule pass: one traversal of the sentence phoneme stream
   that rewrites sounds in context. Unvoiced stops aspirate, stops before a
   pause go unreleased, one vowel reduces at phrase edges, and a glottal stop
   is inserted between certain vowel sequences.
 *
 * It carries five cursors -- the next segment, the one after it, the next
 * stress mark, the next of an eighth class, and a delayed copy of the previous
 * segment -- and reads the stream it is rewriting, so a rewritten byte can
 * change the class its own scans look for and an insertion shifts every
 * position after it. Cursors, rewrites and insertions are one traversal and
 * cannot be separated: an implementation that gets part of it right corrupts
 * the cursors and misplaces everything downstream, which is why a partial
 * version scores below leaving the stream alone.
 *
 * This implementation is partial and is not in the test suite. See the rule
 * mask below for exactly how partial. */

#define RDATA_VA  0x10020000u
#define RDATA_OFF 0x17800u
#define PH_ATTR1  0x10021648u
#define PH_ATTR2  0x100216C8u

#define CMD 0x7C

static int a1(const bst_image *img, int c) {
    size_t o = RDATA_OFF + (PH_ATTR1 - RDATA_VA) + (unsigned)(c & 0xFF);
    return o < img->len ? img->image[o] : 0;
}
static int a2(const bst_image *img, int c) {
    size_t o = RDATA_OFF + (PH_ATTR2 - RDATA_VA) + (unsigned)(c & 0xFF);
    return o < img->len ? img->image[o] : 0;
}

typedef struct { int val, pos; } cur;

/* The three forward scans. Each looks for the next byte carrying a given
   second-attribute bit, skipping empty slots and stepping over the six-byte
   command records, and each gives up slightly differently at the end. */
static void scan_seg(const bst_image *img, const uint8_t *s, int lim, int from, cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (a2(img, b) & 1) { c->val = b; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim - 1 || b == 0x4D || b == 0x4E) { c->val = 0; c->pos = 0; return; }
    }
}

static void scan_stress(const bst_image *img, const uint8_t *s, int lim, int from, cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (a2(img, b) & 2) { c->val = b - 0x30; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim - 1) { c->val = 0; c->pos = 0; return; }
    }
}

static void scan_eight(const bst_image *img, const uint8_t *s, int lim, int from, cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (a2(img, b) & 8) { c->val = b; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim) { c->val = 0; c->pos = 0; return; }
    }
}

/* Which rules are enabled. Two of the five are known wrong and are off by
   default: the glottal-stop insertion (bit 0) and the phrase-edge variant of
   the reduced vowel (bit 4). Each of those scores below leaving the stream
   alone, so enabling them makes the pass worse than not running it.
 *
 * Measured over the corpus, against 5294 of 5568 positions and 28 of 58
 * streams for the identity:
 *     nothing            5294 / 28
 *     aspiration only    5305 / 29
 *     the default here   5306 / 29
 *     everything on      5073 / 12
 *
 * So this reproduces on the order of twelve of the 274 rewrites the pass
 * makes. It is a beginning, not the stage. */
int bst_rule_mask = 0xE;

int bst_phrules(const bst_image *img, uint8_t *s, int len, int cap, int emphasis) {
    cur next = {0, 0}, next2 = {0, 0}, stress = {0, 0}, eight = {0, 0};
    cur pend = {0, 0}, prev = {0, 0};
    cur pend8 = {0, 0}, prev8 = {0, 0};
    int changed = 0, inserted = 0, first = 1;
    int i = 0;
    int lim = len;              /* the engine's stream bound, grows on insert */

    scan_eight(img, s, lim, -1, &eight);

    while (i < lim) {
        int shift = inserted;
        if (first) {
            scan_seg(img, s, lim, i, &next);
            scan_seg(img, s, lim, next.pos, &next2);
            first = 0;
        }
        if (inserted) {
            inserted = 0;
            i += shift;
            pend.pos += shift;
            next.pos += shift;
            next2.pos += shift;
            stress.pos += shift;
            if (next.pos >= 0 && next.pos < lim) next.val = s[next.pos];
            if (next2.pos >= 0 && next2.pos < lim) next2.val = s[next2.pos];
            if (stress.pos >= 0 && stress.pos < lim) stress.val = s[stress.pos];
        }

        if (i == stress.pos) scan_stress(img, s, lim, i, &stress);
        if (pend.pos - i == -1) prev = pend;
        if (i == next.pos) {
            pend = next;
            next = next2;
            scan_seg(img, s, lim, next2.pos, &next2);
        }
        if (pend8.pos - i == -1) prev8 = pend8;
        if (i == eight.pos) {
            pend8 = eight;
            scan_eight(img, s, lim, i, &eight);
        }

        int c = s[i];
        int bit1 = a2(img, c) & 1;

        /* Both comparands of the second flag read zero throughout, so it
           reduces to the attribute bit. */
        int flag_b = bit1;
        int flag_a = bit1 && i <= eight.pos && next.pos > eight.pos;

        if (c == CMD) { i += 7; continue; }

        if (c != 0 && c < 0x31) {
            /* Glottal stop between two vowels of the listed kinds. */
            if ((bst_rule_mask & 1) && flag_b && (a1(img, prev.val) & 1) &&
                prev.val != 0x12 && prev.val != 0x11 && prev.val != 8 && prev.val != 9 &&
                (c == 0x12 || c == 0x11 || c == 8 || c == 9)) {
                if (len + 1 <= cap) {
                    for (int k = len; k > i; k--) s[k] = s[k - 1];
                    s[i] = 0x30;
                    len++; lim++;
                    inserted = 1;
                    changed++;
                }
            }

            /* Unreleased stop. */
            if ((bst_rule_mask & 2) && (c == 0x18 || c == 0x14) && !flag_b &&
                (stress.val < 3 || flag_a) &&
                ((a1(img, prev.val) & 0x80) || prev.val == 0x12) &&
                (a1(img, next.val) & 0x80) && emphasis >= 0) {
                int skip = (c == 0x18 && (next.val == 0x24 || next.val == 0x25) && !flag_a &&
                            next2.val == 0x0F && next2.pos < eight.pos);
                if (!skip && (s[i] == 0x18 || s[i] == 0x14)) {
                    c = 0x19; s[i] = 0x19; changed++;
                }
            }

            /* Aspiration. */
            if ((bst_rule_mask & 4) && emphasis >= 0 &&
                (next.val == 0x0D || next.val == 0x02 ||
                 (eight.val == 0x4E && next.val == 0) ||
                 (((a1(img, c) & 6) == 2) && !flag_b &&
                  ((stress.val < 3 && (c != 0x18 || next.val != 0x12)) ||
                   prev.val == 0x0D || flag_a ||
                   (!(a1(img, next.val) & 0x80) && next.val != 0x12 &&
                    (next.val != 0x11 || c == 0x18) &&
                    next.val != 9 && next.val != 8))))) {
                if (c == 0x16)      { c = 0x17; s[i] = 0x17; changed++; }
                else if (c == 0x18) { c = 0x1B; s[i] = 0x1B; changed++; }
                else if (c == 0x1C) { c = 0x1D; s[i] = 0x1D; changed++; }
            }

            /* The reduced vowel and its phrase-edge variant. */
            if (c == 0x24) {
                if ((bst_rule_mask & 8) && next.val == 0x11) {
                    c = 0x25; s[i] = 0x25; changed++;
                }
                if ((bst_rule_mask & 16) &&
                    (prev.val == 0 || next.val == 0 ||
                     prev.val == 0x2F || next.val == 0x2F)) {
                    c = 0x2B; s[i] = 0x2B; changed++;
                }
            }

            /* The pending previous cursor takes the rewritten value, which is
               how a rewrite reaches the conditions at the next position. */
            pend.val = c;
        }
        i++;
    }
    (void)prev8;
    return changed;
}
