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
 * cannot be separated.
 *
 * The level, set by a command in the stream, gates most of the rules: below
 * zero none of them fire, above nine one of them starts deleting.
 *
 * The cursor scans themselves are shared with the pair scan and live in
 * cursors.c. */

#define CMD 0x7C

typedef bst_cur cur;

static int a1(const bst_image *img, int c) { return bst_ph_attr1(img, c); }
static int a2(const bst_image *img, int c) { return bst_ph_attr2(img, c); }

/* Whether a marker that opens an emphasis group comes before the next phrase
   boundary. */
static int marker_ahead(const bst_image *img, const uint8_t *s, int len, int from) {
    int p = from + 1;
    for (;;) {
        int b = (p >= 0 && p < len) ? s[p] : 0;
        if ((a2(img, b) & 8) || len <= p) return 0;
        if (b == CMD) p += 6;
        else if (b == 0x4F || b == 0x50 || b == 0x51) return 1;
        p++;
    }
}

int bst_rule_mask = -1;   /* retained so a term can still be disabled by hand */

int bst_phrules(const bst_image *img, uint8_t *s, int *lenp, int cap, int level) {
    int len = *lenp;
    int inserted_total = 0;
    cur next = {0, 0}, next2 = {0, 0}, eight = {0, 0}, stress = {0, 0};
    cur prev = {0, 0}, prev8 = {0, 0};
    cur pend = {0, 0}, pend8 = {0, 0}, older8 = {0, 0};
    int changed = 0, inserted = 0, rescan = 1;
    int latch_o = 0, latch_p = 0;
    int lim = len;
    int last = 0;

    /* The traversal stops at the last sound of the eighth class, not at the
       end of the buffer. */
    bst_scan_eight_back(img, s, len, &prev8);
    last = prev8.pos;
    prev8.val = prev8.pos = 0;
    prev.val = prev.pos = 0;
    stress.pos = 0;

    bst_scan_eight(img, s, lim, -1, &eight);

    for (int i = 0; i < last; i++) {
        if (rescan) {
            bst_scan_seg(img, s, lim, i, &next);
            bst_scan_seg(img, s, lim, next.pos, &next2);
            rescan = 0;
        }
        if (inserted) {
            int shift = inserted;
            inserted = 0;
            i += shift;
            pend.pos += shift;
            next.pos += shift;
            next2.pos += shift;
            stress.pos += shift;
            if (next.pos >= 0 && next.pos < lim)   next.val = s[next.pos];
            if (next2.pos >= 0 && next2.pos < lim) next2.val = s[next2.pos];
            if (stress.pos >= 0 && stress.pos < lim) stress.val = s[stress.pos];
        }

        if (i == stress.pos) bst_scan_stress(img, s, lim, i, &stress);
        if (pend.pos - i == -1) prev = pend;
        if (i == next.pos) {
            pend = next;
            next = next2;
            bst_scan_seg(img, s, lim, next2.pos, &next2);
        }
        if (pend8.pos - i == -1) { older8 = prev8; prev8 = pend8; }
        if (i == eight.pos) {
            pend8 = eight;
            bst_scan_eight(img, s, lim, i, &eight);
        }

        int c = s[i];
        int bit1 = a2(img, c) & 1;
        int at_edge = bit1 && !(eight.pos < i) && !(next.pos <= eight.pos);
        int after_seg = bit1 && !(prev8.pos < prev.pos);

        if (c == CMD) {
            if (s[i + 1] == 'q') {
                level = (int16_t)((s[i + 2] << 8) | s[i + 3]);
                if (level > 10)  { level = 10; i += 6; continue; }
                if (level < -10) level = -10;
            }
            i += 6;
            continue;
        }

        if (img->t.ph_kind == 1) {
            /* The Romance rules: no aspiration, no glottal stop, no vowel
               reduction; the voiced stops soften between sounds, the sibilant
               takes the voicing of what follows, and a weak vowel beside
               another vowel drops its stress mark a level. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            bst_cur nstress;
            bst_scan_stress(img, s, lim, i + 2, &nstress);
            int nv = next.val, pv = prev.val;
            int voiced = (a1(img, nv) & 4) && (a1(img, nv) & 1) &&
                         (a1(img, nv) & 0xFA);
            int flank = (a1(img, nv) & 0x80) || (a1(img, pv) & 0x80);
            switch (mc) {
            case 4:
                if (pv != 0 && pv != 0x0A) s[i] = 5;
                break;
            case 6:
                if (pv != 0 && pv != 0x0B && pv != 0x13) s[i] = 7;
                break;
            case 8:
                if (pv != 0 && pv != 0x0D && nv != 0x17) s[i] = 9;
                break;
            case 0x0B:
                if ((nv == 3 || nv == 8 || nv == 0x11 || nv == 0x17) && !after_seg)
                    s[i] = 0x0D;
                break;
            case 0x0F:
                if (voiced) s[i] = 0x10;
                break;
            case 0x10:
                s[i] = voiced ? 0x10 : 0x0F;
                break;
            case 0x18:
                if (stress.val < 5 && flank) s[i] = 0x16;
                break;
            case 0x1C:
                if (stress.val < 5 && flank) s[i] = 0x17;
                break;
            case 0x19: case 0x1A: case 0x1B:
                if (stress.val >= 5) break;
                if (nv == mc && nstress.val < 5) {
                    s[i] = 0;
                    if (i + 1 < lim) s[i + 1] = 0;
                    pend.val = nv;
                } else if (flank && stress.pos > 0) {
                    s[stress.pos] = 0x31;
                }
                break;
            default:
                break;
            }
            pend.val = s[i];
            continue;
        }

        if (img->t.ph_kind == 2) {
            /* The Italian rules: two glides open before a stressed vowel, one
               sound doubles after nothing, and a phrase-final sound takes a
               vowel of its own. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            if (mc == 0x1E) {
                if (stress.val < 3) s[i] = 0x1D;
            } else if (mc == 0x21) {
                if (stress.val < 3) s[i] = 0x20;
            } else if (mc == 0x18) {
                if (eight.val > 0x4C && next.val == 0x2F && len + 3 <= cap) {
                    s[i] = 0x1C;
                    for (int k = len + 2; k > i + 3; k--) s[k] = s[k - 3];
                    s[i + 1] = 0x32;
                    s[i + 2] = 0;
                    s[i + 3] = 0;
                    len += 3; lim += 3;
                    if (i <= last) last += 3;
                    inserted = 3;
                    inserted_total += 3;
                }
            } else if (mc == 0x15) {
                if (prev.val == 0) s[i] = 0x16;
            }
            pend.val = s[i];
            continue;
        }

        if (c == 0x4F && stress.val < 5 && eight.val < 0x4E)      latch_o = 1;
        else if (c == 0x50 && stress.val < 5 && eight.val < 0x4E) latch_p = 1;
        else if (a2(img, c) & 8)                                  latch_o = latch_p = 0;

        if (c == 0 || (c >= 0x31 && c < 0x80)) continue;

        int skip_rest = 0;

        if (a1(img, c) & 0x80) {
            int reduce = 0;
            if (latch_o && level >= 0) reduce = 1;
            else if (latch_p && level > 0 &&
                     !marker_ahead(img, s, lim, older8.pos) &&
                     !marker_ahead(img, s, lim, eight.pos) &&
                     prev8.val != 0x4E && eight.val != 0x4E)
                reduce = 1;
            if (reduce) {
                /* One following sound pre-empts the reduction: this sound
                   becomes a different one and the follower is deleted. */
                if (next.val == 0x12 && next.pos < eight.pos) {
                    c = 0x2E; s[i] = 0x2E;
                    if (next.pos >= 0 && next.pos < lim) s[next.pos] = 0;
                    rescan = 1;
                    changed++;
                } else if (c == 0x2C) {
                    c = 0x2A; s[i] = 0x2A; changed++;
                } else if (!(a1(img, next.val) & 0x80)) {
                    c = 0x24; s[i] = 0x24; changed++;
                }
            }
        }

        /* A glottal stop between two vowels of the listed kinds. */
        if (after_seg && (a1(img, prev.val) & 1) &&
            prev.val != 0x12 && prev.val != 0x11 && prev.val != 8 && prev.val != 9 &&
            (c == 0x12 || c == 0x11 || c == 8 || c == 9)) {
            if (len + 1 <= cap) {
                for (int k = len; k > i; k--) s[k] = s[k - 1];
                s[i] = 0x30;
                len++; lim++;
                if (i <= last) last++;
                inserted = 1;
                inserted_total++;
                changed++;
            }
        }

        /* An unreleased stop. */
        if ((c == 0x18 || c == 0x14) && !after_seg &&
            (stress.val < 3 || at_edge) &&
            ((a1(img, prev.val) & 0x80) || prev.val == 0x12) &&
            (a1(img, next.val) & 0x80) && level >= 0) {
            if (c == 0x18 && (next.val == 0x24 || next.val == 0x25) && !at_edge &&
                next2.val == 0x0F && next2.pos < eight.pos)
                skip_rest = 1;
            else if (s[i] == 0x18 || s[i] == 0x14) {
                c = 0x19; s[i] = 0x19; changed++;
            }
        }
        if (skip_rest) continue;

        /* Aspiration. */
        if ((next.val == 0x0D || next.val == 0x02 ||
             (eight.val == 0x4E && next.val == 0) ||
             (((a1(img, c) & 6) == 2) && !after_seg &&
              ((stress.val < 3 && (c != 0x18 || next.val != 0x12)) ||
               prev.val == 0x0D || at_edge ||
               (!(a1(img, next.val) & 0x80) && next.val != 0x12 &&
                (next.val != 0x11 || c == 0x18) &&
                next.val != 9 && next.val != 8)))) && level >= 0) {
            if (c == 0x16)      { c = 0x17; s[i] = 0x17; changed++; }
            else if (c == 0x18) { c = 0x1B; s[i] = 0x1B; changed++; }
            else if (c == 0x1C) { c = 0x1D; s[i] = 0x1D; changed++; }
        }

        /* The reduced vowel and its phrase-edge variant. */
        if (c == 0x24) {
            if (next.val == 0x11) { c = 0x25; s[i] = 0x25; changed++; }
            if (level > 9) {
                s[i] = 0;
                if (i + 1 < lim) s[i + 1] = 0;
                prev = next;
                changed++;
            }
            if (prev.val == 0 || next.val == 0 ||
                prev.val == 0x2F || next.val == 0x2F) {
                c = 0x2B; s[i] = 0x2B; changed++;
            }
        }

        pend.val = c;
    }
    (void)latch_p;
    (void)bst_rule_mask;
    (void)changed;
    *lenp = len;
    return inserted_total;
}
