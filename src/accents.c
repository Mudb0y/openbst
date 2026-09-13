#include <string.h>
#include "bst_text.h"

/* Accent assignment: writing the pitch-target codes into the sentence stream.
 *
 * Every group opener in the stream reserves two empty slots after it, and this
 * pass fills some of them with codes in the range 0x3A to 0x48. The contour
 * generator reads them back as offsets from 0x3D into a fourteen-entry pitch
 * table, so a code is a pitch level rather than a frequency.
 *
 * Which accents get codes is decided in two sweeps. The first walks the stream
 * collecting one entry per group opener, classifying each by the mark that
 * follows it and by the emphasis commands in force, and picking out three
 * landmarks: the first accent that counts, the last, and the last of all. The
 * second walks from the first landmark to the last writing codes, with the
 * sentence type -- statement, question, exclamation, carried from the type
 * command at the head of the stream -- choosing between rise and fall at each
 * step and at the end. */

#define ACC_MAX 64

typedef struct { short pos; signed char kind; } acc;

static int cmd16(const uint8_t *s, int i) { return (s[i + 2] << 8) | s[i + 3]; }

/* Backward to the previous segment, giving up at the start of the stream or
   at a phrase marker. */
static void scan_back_seg(const bst_image *img, const uint8_t *s, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q, b;
        do {
            q = p; p = q - 1;
            if (p < 0) { c->val = 0; c->pos = 0; return; }
            b = s[p];
        } while (b == 0);
        if (bst_ph_attr2(img, b) & 1) { c->val = b; c->pos = p; return; }
        if (b == 0x7C) { p = q - 7; continue; }
        if (p < 1 || b == 0x4D || b == 0x4E) { c->val = 0; c->pos = 0; return; }
    }
}

int bst_accents(const bst_image *img, uint8_t *s, int len, bst_accent_state *st) {
    acc e[ACC_MAX];
    short bcmd[16];
    int nb = 0, n = 0;
    int first = 0, last = 0, latest = 0;     /* the three landmarks */
    int seen = 0;                            /* the running "last that counts" */
    int suppress = 0;                        /* inside a bracket that mutes accents */
    int soft = 0;                            /* a mark has opened the first accent */
    int f1d = 0, f1e = 0, f1f = 0;           /* the three tilde modes */
    int emph = 0;
    int type, shape;
    int codeA = 0, codeB = 0;

    memset(e, 0, sizeof e);
    st->emphasis = 0;

    /* A stream flagged as a fragment carries no contour of its own; it only
       hands the following one a starting level. */
    if (st->flags & 8) {
        if (s[4] == 0) s[4] = (uint8_t)(st->level + 0x3D);
        if (st->tail >= 0 && st->tail < len) {
            if (s[st->tail] != 0) return 0;
            s[st->tail] = (uint8_t)(st->level + 0x3D);
        }
        return 0;
    }

    if (s[6] == 'I') {
        type  = s[8] + 0x0E;
        shape = s[9] & 0x7F;
    } else {
        shape = 0x4C;
        type  = 0x12;
    }

    bst_cur lastseg;
    scan_back_seg(img, s, len - 6, &lastseg);

    for (int i = 12; i < len; ) {
        int b = s[i];
        int next = i;
        if (b == 0x7C) {
            int c = s[i + 1];
            if (c == 'D') emph = cmd16(s, i) >= 1;
            else if (c == 'b' && nb < 10) bcmd[++nb] = (short)i;
            else if (c == '~') {
                int v = cmd16(s, i);
                if (v == 0x1D) f1d = 1;
                else if (v == 0x1E) f1e = 1;
                else if (v == 0x1F) f1f = 1;
            }
            next = i + 6;
            if (bst_ph_attr2(img, s[i + 7]) & 8) next = i + 7;
        } else if (bst_ph_attr2(img, b) & 0x10) {
            if (b == 0x4F || b == 0x50 || b == 0x51) suppress = 1;
        } else if (bst_ph_attr2(img, b) & 8) {
            if (b == 0x4B) suppress = 0;
            else { suppress = f1d = f1e = f1f = 0; }
        } else if (bst_ph_attr1(img, b) & 0x80) {
            if (n + 1 >= ACC_MAX) break;
            n++;
            e[n].pos = (short)(i + 1);
            e[n].kind = 0;
            int mark = s[i + 1];
            int t = seen;
            int tail = 0;
            if (mark < 0x36) {
                if (mark == 0x35) {
                    if (first != 0 || suppress) { tail = suppress; }
                    else { e[n].kind = 3; soft = 1; first = n; }
                }
            } else {
                if (!suppress) {
                    if (f1e)      { e[n].kind = 1; if (soft && last == 0) first = n; }
                    else if (f1f) { e[n].kind = 2; if (soft && last == 0) first = n; }
                    else            e[n].kind = 3;
                    if (first == 0) first = n;
                    t = n;
                    if (!f1d) { t = seen; if (seen == 0) last = n; }
                }
                tail = 1;
            }
            if (tail) { latest = n; seen = t; }
            next = i + 3;
        }
        i = next + 1;
    }

    /* Nothing to accent: hand the level on and stop. */
    if (n == 0) {
        int c = st->carried ? st->carried : st->level + 0x3D;
        if (s[4] == 0) s[4] = (uint8_t)c;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = (uint8_t)c;
        return 0;
    }

    if (seen == 0) {
        if (last == 0 && type != 0x13) {
            int k = latest ? latest : n;
            first = k;
            e[k].kind = 3;
            last = k;
            seen = k;
        } else if (first == 0 || last == 0) {
            int c = st->carried ? st->carried : st->level + 0x3D;
            if (s[4] == 0) s[4] = (uint8_t)c;
            if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
                s[st->tail] = (uint8_t)c;
            return 0;
        } else {
            seen = last;
        }
    }
    last = seen;

    int kind = e[first].kind;
    int nextkind = 0;
    for (int k = first; k <= last; k++) {
        nextkind = e[k].kind;
        if (nextkind != 0) break;
    }

    /* A bracket command before the first accent sets the pitch level. */
    for (int k = nb; k >= 1; k--)
        if (bcmd[k] < e[first].pos) { st->level = s[bcmd[k] + 3]; break; }

    int lead;
    if (first == 1) {
        lead = (kind == 1 || kind == 2) ? 0x3C : st->level + 0x3D;
        if (kind == 2) codeA = 0x3D;
    } else if (last == first || kind == 1 || nextkind == 1) {
        if (shape == 0x48 || (shape == 0x52 && (kind == 1 || kind == 2))) lead = 0x42;
        else lead = 0x3D;
        if ((!emph || (kind != 3 && kind != 2) || shape == 0x4C) &&
            (shape != 0x48 || kind != 1)) {
            if (shape == 0x4C && (kind == 3 || kind == 2))      codeA = 0x3D;
            else if (shape == 0x48 && kind == 2)                codeA = 0x42;
            else if (shape == 0x52 && kind == 2)                codeA = 0x3D;
            else                                                codeA = 0;
        } else codeA = 0x42;
    } else {
        lead = 0x3D;
        codeA = 0x3D;
    }

    if (s[4] == 0) {
        s[4] = (uint8_t)lead;
        if (st->carried) {
            s[4] = (uint8_t)st->carried;
            if (s[0] == 0x4E && first > 1) {
                if (last == first &&
                    (shape == 0x48 || (shape == 0x52 && (kind == 1 || kind == 2))))
                    s[e[1].pos + 2] = 0x42;
                else
                    s[e[1].pos + 2] = 0x3D;
            }
        }
    }

    int cur = first;
    if (kind == 2) {
        s[e[first].pos + 1] = (uint8_t)codeA;
    } else if (first > 1) {
        if (codeA < 0x42) s[e[first - 1].pos + 1] = (uint8_t)codeA;
        else              s[e[first - 1].pos + 2] = (uint8_t)codeA;
    }

    while (cur < last && last != first) {
        int nx = cur;
        int nk;
        do { nx++; nk = e[nx].kind; } while (nk == 0 && nx < last);

        for (int k = nb; k >= 1; k--)
            if (e[cur].pos < bcmd[k] && bcmd[k] < e[nx].pos) {
                st->level = s[bcmd[k] + 3];
                break;
            }

        if (shape == 0x4C && (kind == 3 || kind == 2) && (nk == 3 || nk == 2))
            codeB = st->level + 0x3D;
        else
            codeB = 0;

        if (nk == 2) codeA = 0x3D;
        else if (last == nx && (shape == 0x48 || nx - cur == 1) &&
                 (kind == 3 || kind == 2)) codeA = 0x42;
        else if (shape == 0x48 || ((kind != 3 && kind != 2) || nk != 3)) {
            int keep = (emph && kind == 1 && nk == 3) ||
                       (shape == 0x48 && (kind == 3 || kind == 2) && nk == 1) ||
                       (emph && shape == 0x48 && (kind == 3 || kind == 2) && nk == 3);
            if (!keep) codeA = 0;
            else if (last == nx)
                codeA = (!emph && type != 0x0F && type != 0x10) ? 0x43 : 0x42;
            else codeA = 0x42;
        } else {
            codeA = st->level + 0x3D;
        }

        if (kind == 2) s[e[cur].pos + 2] = 0x42;
        else           s[e[cur].pos + 1] = (uint8_t)(kind == 1 ? 0x3C : 0x42);

        kind = nk;
        int prev = cur;
        cur = nx;
        if (nk == 2) {
            s[e[nx].pos + 1] = 0x3D;
        } else if (nx - prev < 2) {
            if (last == nx) s[e[nx - 1].pos + 2] = (uint8_t)codeA;
        } else {
            if (codeA < 0x42 && shape != 0x52) s[e[nx - 1].pos + 1] = (uint8_t)codeA;
            else                               s[e[nx - 1].pos + 2] = (uint8_t)codeA;
            if (nx - prev > 2) s[e[prev + 1].pos + 1] = (uint8_t)codeB;
        }
    }

    for (int k = nb; k >= 1; k--)
        if (e[last].pos < bcmd[k]) { st->level = s[bcmd[k] + 3]; break; }

    int fin;
    if (kind == 1) {
        codeB = 0;
        fin = 0x3C;
        codeA = (type == 0x0F || type == 0x11) ? 0x42 : 0x3C;
    } else {
        if (emph || type == 0x0F)      fin = 0x42;
        else                           fin = (type == 0x10) ? 0x42 : 0x43;
        switch (type) {
        case 0x0F: codeB = 0;                 codeA = 0x45; break;
        case 0x10: codeB = 0;                 codeA = 0x42; break;
        case 0x11: codeB = 0x3D;              codeA = 0x42; break;
        case 0x16: codeA = 0x45;              codeB = st->level + 0x3D; break;
        case 0x14: codeB = 0;                 codeA = st->level + 0x3D; break;
        case 0x17: codeA = st->level + 0x3D;  codeB = codeA; break;
        case 0x13: codeB = 0;                 codeA = 0x3D; break;
        case 0x12: codeB = 0;
                   codeA = (lastseg.val == 0x2F) ? 0x3A : 0x3D; break;
        default: break;
        }
    }

    if (kind == 2) s[e[cur].pos + 2] = (uint8_t)fin;
    else           s[e[cur].pos + 1] = (uint8_t)fin;

    int done = 0;
    if (type == 0x11 || type == 0x13 || type == 0x12) {
        if (kind == 3) { s[e[cur].pos + 2] = 0x3D; done = 1; }
        else if (kind == 2) {
            if (n != cur && n - cur >= 0) s[e[cur + 1].pos + 1] = 0x3D;
            done = 1;
        }
    }
    if (!done && (type == 0x16 || type == 0x17))
        s[e[cur + 1].pos + 1] = (uint8_t)codeB;

    int rest = n - cur;
    if (rest > 0 && (kind != 2 || rest > 1)) s[e[n].pos + 1] = (uint8_t)codeB;

    if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
        s[st->tail] = (uint8_t)codeA;

    st->carried = (lastseg.val == 0x2F) ? 0 : codeA;
    st->emphasis = emph;
    return n;
}
