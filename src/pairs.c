#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* The pair scan: one traversal of the sentence stream that turns sounds into
   the two record streams the acoustic stage consumes.
 *
 * For each sound it emits a diphone segment naming the previous sound and this
 * one, and up to three transition events carrying a duration and two pitch
 * offsets. Which events come before the segment and which after is fixed per
 * sound class, and the segment records how many events have accumulated since
 * the last one, so the two streams interleave by construction.
 *
 * Durations are not stored: a vowel's comes out of a table scaled by stress,
 * by whether the sentence is winding down, and by what follows it, and a
 * consonant's is a fixed hundred bent by half a dozen context tests. Those
 * tests read the same cursors the rule pass uses, one of which this scan
 * mutates as it goes, so they cannot be lifted out and applied afterwards. */


#define CMD 0x7C

typedef struct {
    const bst_image *img;
    const uint8_t   *s;
    int              len;      /* the stream bound the scans respect */
    int              last;     /* the final position the traversal reaches */

    bst_cur next, next2, eight, strong, stress, prev, prev8, prevstrong;
    bst_cur pend, pend8, pendstrong;

    int edge;      /* this sound closes a phrase-final run */
    int begins;    /* the sound opens a group */
    int group;     /* inside a group opened by one of the marker sounds */
    int pending;   /* transition events since the last segment */
    int emphasis;
    int flags;
    int prevph;

    bst_emit *out;
    int       n, max;
    int       overflow;
} scan;

/* Both take a byte offset from the table's address, not an index: several of
   these tables are read at a stride that is not their element size. */
static int s8at(const bst_image *img, unsigned va, unsigned i) {
    return (int8_t)bst_u8(img, va, (int)i);
}
static int s16at(const bst_image *img, unsigned va, unsigned i) {
    const uint8_t *p = bst_at(img, va + i, 2);
    return p ? (int16_t)(p[0] | (p[1] << 8)) : 0;
}

static int a1(scan *z, int c) { return bst_ph_attr1(z->img, c); }
static int a2(scan *z, int c) { return bst_ph_attr2(z->img, c); }

static void emit(scan *z, uint8_t kind, unsigned idx, unsigned cnt, int a, int b) {
    if (z->n >= z->max) { z->overflow = 1; return; }
    z->out[z->n].kind  = kind;
    z->out[z->n].index = (uint16_t)idx;
    z->out[z->n].count = (uint16_t)cnt;
    z->out[z->n].a     = (int16_t)a;
    z->out[z->n].b     = (int16_t)b;
    z->n++;
}

/* Whether the sentence is winding down: two while a phrase-final fall is
   pending, one inside a group, zero otherwise. */
static int rate_mode(scan *z) {
    if (z->strong.val == 0 && z->eight.val == 0x4E) return 2;
    return z->prevstrong.val == 0 ? 0 : 1;
}

/* The sound and the ones either side of it are all of the class that carries
   a formant target, so the transition is a glide rather than a release. */
static int glide(scan *z) {
    return (a1(z, z->next.val) & 1) && z->next.val != 9 && z->next.val != 8 &&
           (a1(z, z->next.val) & 4);
}

static int glide_run(scan *z) {
    if (!glide(z)) return 0;
    for (int p = z->next.pos; p <= z->eight.pos; p++) {
        int a = a1(z, z->s[p]);
        if (((a & 1) && !(a & 4)) || (a & 0x80)) return 1;
    }
    return 0;
}

/* An unstressed sound between a group opener and a real neighbour: the one
   case where the transition is halved rather than stretched. */
static int weak(scan *z, int pos) {
    int c = z->s[pos];
    int follows = a1(z, z->next.val) & 0x80;
    if (!(((z->stress.val < 3 && z->stress.val != 0) && follows) ||
          (z->edge && follows)))
        return 0;
    if (z->prev.val == 0 || c == 8 || c == 10) return 0;
    if (c == 0x12 || c == 9 || c == 0x19) return 0;
    return 1;
}

/* One of four specific sounds flanked by its own kind. */
static int paired(int ph, int other) {
    switch (ph) {
    case 1:    return other == 4;
    case 2:    return other == 5;
    case 3:    return other == 7;
    case 0x0D: return other == 6;
    default:   return 0;
    }
}

static int flanked(scan *z, int pos) {
    if (z->prev.val == 0 || z->prev.val > 0x2E) return 0;
    if (z->next.val == 0 || z->next.val > 0x2E) return 0;
    int ph = z->s[pos];
    if ((a1(z, ph) & 0x44) != 0x40) return 0;
    if ((a1(z, z->prev.val) & 0x44) != 0x40 && (a1(z, z->next.val) & 0x44) != 0x40 &&
        !paired(ph, z->prev.val) && !paired(ph, z->next.val))
        return 0;
    return 1;
}

static int between_vowels(scan *z, int pos) {
    int ph = z->s[pos];
    if (!(a1(z, ph) & 1)) return 0;
    if (((!(a2(z, ph) & 1) || z->prev8.pos < z->prev.pos) && (a1(z, z->prev.val) & 1)) ||
        ((a1(z, z->next.val) & 1) && (!z->edge || z->stress.val < 5)))
        return 1;
    return 0;
}

/* Does a run of sounds start at this position. Walks back to the previous
   segment or group boundary. */
static int opens_group(scan *z, int pos) {
    if (!(a2(z, z->s[pos]) & 1)) return 0;
    int p = pos;
    for (;;) {
        int q;
        int b;
        do {
            q = p; p = q - 1;
            if (p == 0) return 1;
            b = z->s[p];
        } while (b == 0);
        if (b == CMD) { p = q - 7; continue; }
        if (a2(z, b) & 1) return 0;
        if (a2(z, b) & 8) return 1;
    }
}

/* The vowel-context test that decides whether a vowel takes its table
   duration or the scaled one. It rewrites the next cursor on the way, which
   is why it cannot be hoisted. */
static int vowel_context(scan *z) {
    int v = z->begins;
    if (z->next.val == 0x12 && (z->next2.val == 0x0F || z->next2.val == 0x0E)) {
        z->next.val = 0x0F;
        bst_scan_seg(z->img, z->s, z->len, z->next2.pos, &z->next2);
        v = opens_group(z, z->next2.pos);
    }
    if (z->edge || v || (a1(z, z->next2.val) & 4)) return 0;

    int n2 = z->next2.val;
    switch (z->next.val - 0x0E) {
    case 0: return n2 == 2 || (n2 >= 0x16 && n2 <= 0x17);
    case 1: return n2 == 2 || n2 == 3 || n2 == 0x0B || n2 == 0x0D ||
                   n2 == 0x18 || n2 == 0x19 || n2 == 0x1B;
    case 2: return n2 == 2 || (n2 >= 0x1C && n2 <= 0x1D);
    case 3: return n2 == 2 || n2 == 3 || n2 == 0x0B || n2 == 0x0D ||
                   n2 == 0x18 || n2 == 0x19 || n2 == 0x1B || n2 == 0x1C || n2 == 0x1D;
    case 4: return n2 == 1 || n2 == 2 || n2 == 3 || n2 == 0x0B || n2 == 0x0D ||
                   n2 == 0x16 || n2 == 0x17 || n2 == 0x18 || n2 == 0x19 ||
                   n2 == 0x1B || n2 == 0x1C || n2 == 0x1D;
    case 6: return n2 == 2;
    default: return 0;
    }
}

/* Signed division by four rounding toward zero, as the original open-codes it
   with a shift and a bias. */
static int div4(int v) { return (int16_t)((v + ((v >> 31) & 3)) >> 2); }

static int d16(int v) { return (int16_t)v; }

static int vowel_duration(scan *z, int pos, int which) {
    int nv = z->next.val;
    int ph = z->s[pos];
    int base = (uint16_t)s16at(z->img, z->img->t.vowel_dur, (unsigned)(ph * 3 + which) * 2);
    int mode = rate_mode(z);
    int d;
    int scaled = 0;

    if (nv == 0 || (a1(z, nv) & 4) || nv == 0x2F || z->edge) {
        if (!vowel_context(z)) {
            int sh = z->img->t.stress_shift;
            int e;
            if (sh) {
                /* The same scaling reached by shifts, which floor where the
                   earlier builds' divides truncate. */
                d = (int16_t)(d16(base) + (d16(base) >> 2));
                if (mode == 2) d = (int16_t)(d + (((int16_t)d * 13) >> 5));
                int scale = bst_u8(z->img, z->img->t.stress_num,
                                   (unsigned)z->stress.val);
                e = (int16_t)((int16_t)(scale * d) >> sh);
            } else {
                d = div4((int16_t)(base * 5));
                if (mode == 2) d = (int16_t)(d * 7) / 5;
                int num = s16at(z->img, z->img->t.stress_num, (unsigned)z->stress.val * 4);
                int den = s16at(z->img, z->img->t.stress_num + 2, (unsigned)z->stress.val * 4);
                e = den ? (int16_t)(num * d) / den : 0;
            }
            if (!z->edge) {
                d = e;
                if ((a1(z, nv) & 0x40) && z->stress.val > 5) {
                    if (mode == 2)
                        d = sh ? (int16_t)((e + 0x5F) - (((int16_t)(e + 0x5F)) >> 2))
                               : div4((int16_t)((e + 0x5F) * 3));
                    else          d = e + 0x19;
                }
            } else {
                d = e + 0x32;
                if ((a1(z, z->next.val) & 0x80) && z->stress.val > 3) d = e + 0x50;
            }
            scaled = 1;
        }
    }
    if (!scaled) {
        if (a1(z, nv) & 0x40) base += 0x19;
        d = base + s16at(z->img, z->img->t.stress_add, (unsigned)z->stress.val * 2);
    }

    if (mode == 2)      d += 0x41;
    else if (mode == 0) d += 0x14;
    d += s16at(z->img, z->img->t.sound_add, (unsigned)z->s[pos] * 2) - 0x3C;

    if ((ph == 0x1F || ph == 0x20 || ph == 0x21) && d < 0x37) d = 0x37;
    else if (ph == 0x24 && d < 5) d = 5;
    else if (d < 0x1E) d = 0x1E;
    if (bst_trace)
        fprintf(stderr, "vdur ph=%02x which=%d base=%02x stress=%d mode=%d"
                        " edge=%d nv=%02x -> %02x\n",
                ph, which, base, z->stress.val, mode, z->edge, nv,
                (unsigned)((int16_t)d >> z->img->t.vowel_dur_shift) & 0xff);
    return (int16_t)((int16_t)d >> z->img->t.vowel_dur_shift);
}

/* One transition event. Position 0 falls before the sound, 1 and 2 after. */
static void trans(scan *z, int dur, int pos, int which) {
    z->pending = (z->pending + 1) & 0xFF;
    int ph = z->s[pos];
    int nudge = 100;

    if (z->emphasis && (a1(z, ph) & 2) && which == 2) dur >>= 2;

    int at = a1(z, ph);
    if (!(at & 0x80)) {
        int mul = 0;
        if ((((at & 0x22) == 0 || which != 0) && ((at & 0x10) == 0 || which != 0)) &&
            (at & 0x32)) {
            if ((ph == 4 || ph == 5 || ph == 6 || ph == 7) &&
                which != 1 && weak(z, pos))
                mul = 7;
        } else if (weak(z, pos)) {
            dur >>= 1;
            if ((at & 0x44) == 0x40 && (a1(z, z->next.val) & 0x80) &&
                (a1(z, z->prev.val) & 0x80))
                nudge = 0x91;
        } else if (flanked(z, pos)) {
            mul = 7;
        } else if (between_vowels(z, pos) &&
                   !((at & 0x12) && ((a1(z, z->next.val) & 0x12) ||
                                     (a1(z, z->prev.val) & 0x12)))) {
            mul = 6;
        }
        if (mul) {
            if (z->img->t.dur_frac_shift)
                dur = mul == 6 ? (dur * 19) >> 5 : (dur * 11) >> 4;
            else
                dur = dur * mul / 10;
        }
    } else if (which == 2 && glide_run(z)) {
        dur = 0x3C;
    }

    unsigned k = (unsigned)(((a1(z, z->next.val) & 0x80) == 0) + ph * 2);
    int c8 = s8at(z->img, z->img->t.trans_pitch, k * 6 + (unsigned)which);
    int c7 = s8at(z->img, z->img->t.trans_pitch + 3, k * 6 + (unsigned)which);
    if (c8 != 0x7F) c8 = (int8_t)(c8 + (nudge - 100) / 5);

    /* A sound with no spectral target of its own leaves the contour to its
       neighbours, so its offsets drop out. */
    if (bst_ph_attr1(z->img, ph) & 0x80 || ph == 8 || ph == 9 || ph == 0x12) {
        int prev_open = (a1(z, z->prev.val) & 0x80) || z->prev.val == 8 ||
                        z->prev.val == 9 || z->prev.val == 0x12;
        int next_open = (a1(z, z->next.val) & 0x80) || z->next.val == 8 ||
                        z->next.val == 9 || z->next.val == 0x12;
        if ((!prev_open || z->prev.val == 0) && which == 0) {
            c8 = (int8_t)(c8 - 5);
        } else if ((!next_open || z->prev.val == 0) && which == 2) {
            c8 = 0x7F; c7 = (int8_t)(c7 - 5);
        } else if (which == 0) {
            c8 = 0x7F; c7 = 0x7F;
        } else {
            c8 = 0x7F;
            if (which == 2) c7 = 0x7F;
        }
    }

    int d = (int16_t)((int16_t)(dur - (dur >> 4)) + z->img->t.trn_round) >> 1;
    emit(z, BST_EMIT_TRANS, (unsigned)((ph - 1) * 3 + which), (unsigned)d, c8, c7);
}

/* One diphone segment, carrying the events accumulated since the last. */
static void pair(scan *z, int prev, int cur) {
    /* The inventory's size is the stride, and it is the size of this
       language's inventory rather than English's. */
    int stride = 0x30 + z->img->t.code_shift;
    emit(z, BST_EMIT_SEG,
         (unsigned)(bst_code(z->img, prev) * stride + bst_code(z->img, cur)),
         (unsigned)z->pending, 0, 0);
    z->pending = 0;
}

static void command(scan *z, const uint8_t *b) {
    switch (b[0]) {
    case 0x44: case 0x49: case 0x50: case 0x62:
    case 0x66: case 0x68: case 0x6C:
        return;
    default:
        emit(z, BST_EMIT_SEG, (unsigned)(b[0] | 0xF000), 0,
             (b[1] << 8) | b[2], (b[3] << 8) | b[4]);
    }
}

int bst_pairs(const bst_image *img, const uint8_t *stream, int len,
              bst_pair_state *st, bst_emit *out, int max) {
    scan zz;
    scan *z = &zz;
    memset(z, 0, sizeof *z);
    z->img = img; z->s = stream; z->len = len;
    z->out = out; z->max = max;
    z->emphasis = st->emphasis;
    z->flags = st->flags;
    z->prevph = st->prev;
    z->strong.val = st->strong;

    /* The traversal runs to the last sound of the eighth class, not to the
       end of the buffer. */
    bst_scan_eight_back(img, stream, len, &z->prev8);
    z->last = z->prev8.pos;
    z->prev8.val = z->prev8.pos = 0;
    z->strong.pos = 0;
    z->stress.pos = 0;

    bst_scan_eight(img, stream, len, -1, &z->eight);
    bst_scan_seg(img, stream, len, 0, &z->next);
    bst_scan_seg(img, stream, len, z->next.pos, &z->next2);
    z->prev.val = z->prev.pos = 0;
    z->prevstrong.val = z->prevstrong.pos = 0;

    if (z->last < 0) { st->prev = z->prevph; return 0; }

    for (int i = 0; i <= z->last; i++) {
        if (z->stress.pos == i) bst_scan_stress(img, stream, len, i, &z->stress);
        if (z->pend.pos - i == -1) z->prev = z->pend;
        if (z->next.pos == i) {
            z->pend = z->next;
            z->next = z->next2;
            bst_scan_seg(img, stream, len, z->next.pos, &z->next2);
        }
        if (z->pend8.pos - i == -1) z->prev8 = z->pend8;
        if (z->eight.pos == i) {
            z->pend8 = z->eight;
            bst_scan_eight(img, stream, len, i, &z->eight);
        }
        if (z->pendstrong.pos - i == -1) z->prevstrong = z->pendstrong;
        if (z->strong.pos == i) {
            z->pendstrong = z->strong;
            bst_scan_strong(img, stream, len, i, &z->strong);
        }

        if (!(a2(z, stream[i]) & 1) || z->eight.pos < i) z->edge = 0;
        else z->edge = z->next.pos > z->eight.pos;

        z->begins = opens_group(z, z->next2.pos);

        int c = stream[i];
        int carry = z->prevph;

        if (c == CMD) {
            command(z, stream + i + 1);
            i += 6;
            continue;
        }

        if ((a2(z, c) & 0x10) && (c == 0x4F || c == 0x50 || c == 1)) z->group = 1;
        else if (a2(z, c) & 8) z->group = 0;

        /* Only the sound codes proper emit anything or advance the previous
           sound. The stream also carries stress marks, phrase markers and the
           intonation records written after the rule pass, all above this
           range, and the scan steps straight over them. */
        if (c >= 1 && c <= 0x30) {
            carry = c;
            switch (c) {
            case 0x0E: case 0x0F: case 0x10:
                pair(z, z->prevph, c);
                trans(z, 100, i, 0);
                break;
            case 0x13: case 0x14: case 0x15:
            case 0x17: case 0x1B: case 0x1D:
                trans(z, 100, i, 0);
                pair(z, z->prevph, c);
                trans(z, 100, i, 1);
                break;
            case 0x19: case 0x1A:
                trans(z, 100, i, 0);
                pair(z, z->prevph, c);
                break;
            case 0x30:
                pair(z, z->prevph, c);
                break;
            case 0x2F:
                pair(z, z->prevph, c);
                trans(z, 100, i, 1);
                break;
            default:
                if (c >= 0x1E && c <= 0x2E) {
                    trans(z, 100, i, 0);
                    pair(z, z->prevph, c);
                    trans(z, vowel_duration(z, i, 1), i, 1);
                    trans(z, 100, i, 2);
                } else if (c <= 0x0D || c == 0x11 || c == 0x12 ||
                           c == 0x16 || c == 0x18 || c == 0x1C) {
                    trans(z, 100, i, 0);
                    pair(z, z->prevph, c);
                    trans(z, 100, i, 1);
                    trans(z, 100, i, 2);
                }
                break;
            }
        }
        z->prevph = carry;
    }

    /* The closing pair. A sentence that ends without a real sound closes on
       silence, but only what the next sentence starts from changes: the pair
       itself still names the last sound emitted. */
    bst_scan_seg(img, stream, len, z->last, &z->next);
    int carry = z->prevph;
    if (stream[z->last] == 0x4E || z->next.val == 0) {
        z->next.val = 0x30;
        carry = 0x30;
    }
    pair(z, z->prevph, z->next.val);

    st->prev = carry;
    st->strong = z->strong.val;
    return z->overflow ? -1 : z->n;
}
