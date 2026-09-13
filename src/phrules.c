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
 * The cursor scans themselves are shared with the pair scan and live in
 * cursors.c.
 *
 * This implementation is partial and is not in the test suite. See the rule
 * mask below for exactly how partial. */

#define CMD 0x7C

typedef bst_cur cur;

static int a1(const bst_image *img, int c) { return bst_ph_attr1(img, c); }
static int a2(const bst_image *img, int c) { return bst_ph_attr2(img, c); }

#define scan_seg(i, s, l, f, c)    bst_scan_seg(i, s, l, f, c)
#define scan_stress(i, s, l, f, c) bst_scan_stress(i, s, l, f, c)
#define scan_eight(i, s, l, f, c)  bst_scan_eight(i, s, l, f, c)

/* Which rules and which terms are enabled. Every condition has its own bit so
 * they can be measured separately, which is the only way to tell a correct
 * rule from a broken one here: one over-firing term swamps several correct
 * ones, because a wrong rewrite costs twice, as a miss and as a false
 * positive.
 *
 * Measured over the corpus, against 5294 of 5568 positions and 28 of 58
 * streams for the identity:
 *     nothing                     5294 / 28
 *     the default here            5565 / 55
 *     plus the empty-next test    5544 / 41
 *
 * One term remains wrong and is off, bit 5, which tests the next cursor for
 * emptiness in the phrase-edge rule. Everything else is on.
 *
 * That is 271 of the 274 rewrites the pass makes, and 55 of 58 sentences
 * reproduced exactly. Not yet in the suite: three sentences still differ. */
int bst_rule_mask = 0x15F;

int bst_phrules(const bst_image *img, uint8_t *s, int len, int cap, int emphasis) {
    cur next = {0, 0}, next2 = {0, 0}, stress = {0, 0}, eight = {0, 0};
    cur pend = {0, 0}, prev = {0, 0};
    cur pend8 = {0, 0}, prev8 = {0, 0};
    int changed = 0, inserted = 0, first = 1;
    /* Two latches carried across positions, set by the two marker codes and
       cleared by any segment of the eighth class. */
    int latch_o = 0, latch_p = 0;
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

        /* The second flag compares the positions of the previous segment and
           the previous eighth-class segment. Both read zero at the pass's
           entry, which made them look constant; they are not. */
        int flag_b = bit1 && prev.pos <= prev8.pos;
        int flag_a = bit1 && i <= eight.pos && next.pos > eight.pos;

        if (c == CMD) { i += 7; continue; }

        if (c == 0x4F && stress.val < 5 && eight.val < 0x4E)      latch_o = 1;
        else if (c == 0x50 && stress.val < 5 && eight.val < 0x4E)  latch_p = 1;
        else if (a2(img, c) & 8)                                   latch_o = latch_p = 0;

        if (c != 0 && c < 0x31) {
            /* Segments that open a group: one marker maps to another, and
               otherwise the segment reduces when what follows does not open a
               group of its own. This is the commonest rewrite in the pass, and
               leaving it out was what made the previous-segment cursor look
               broken -- prev picks up the rewritten value. */
            if ((bst_rule_mask & 256) && (a1(img, c) & 0x80) &&
                latch_o && emphasis >= 0) {
                /* A following segment of one particular kind pre-empts the
                   rewrite: the current sound becomes a different one and the
                   follower is deleted outright. */
                int preempt = 0;
                if (next.val == 0x12 && next.pos < eight.pos) {
                    c = 0x2E; s[i] = 0x2E;
                    if (next.pos >= 0 && next.pos < lim) s[next.pos] = 0;
                    first = 1;
                    changed++;
                    preempt = 1;
                }
                if (!preempt) {
                    if (c == 0x2C) { c = 0x2A; s[i] = 0x2A; changed++; }
                    else if (!(a1(img, next.val) & 0x80)) {
                        c = 0x24; s[i] = 0x24; changed++;
                    }
                }
            }

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
                /* These test the cursor as a whole, not just its value: a
                   cursor reads zero only when its scan found nothing. */
                int edge = 0;
                if ((bst_rule_mask & 16)  && prev.val == 0 && prev.pos == 0) edge = 1;
                if ((bst_rule_mask & 32)  && next.val == 0 && next.pos == 0) edge = 1;
                if ((bst_rule_mask & 64)  && prev.val == 0x2F && prev.pos == 0) edge = 1;
                if ((bst_rule_mask & 128) && next.val == 0x2F && next.pos == 0) edge = 1;
                if (edge) { c = 0x2B; s[i] = 0x2B; changed++; }
            }

            /* The pending previous cursor takes the rewritten value, which is
               how a rewrite reaches the conditions at the next position. */
            pend.val = c;
        }
        i++;
    }
    (void)latch_p;
    return changed;
}
