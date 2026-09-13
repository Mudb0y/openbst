#include <string.h>
#include "bst_text.h"

/* Letter-to-sound rules, in the NRL style. At each position the engine tries a
   two-letter key then a one-letter key, hashes it modulo 237 into a dispatch
   table, and walks a chain of candidate rules. A rule fires when its right
   context matches forwards from just past the key and its left context matches
   backwards from just before it.
 *
 * Rules carry a priority, lower being better. Both key lengths are tried and
 * the best priority wins, so a one-letter rule can beat a two-letter one, and
 * within a chain the walk stops at the first rule whose priority is no better
 * than the best so far. */

#define RDATA_VA  0x10020000u
#define RDATA_OFF 0x17800u
#define DATA_VA   0x10096000u
#define DATA_OFF  0x8CC00u

#define IS_LETTER   0x10021020u   /* bit 3 marks a letter */
#define LETTER_ATTR 0x10021120u
#define SUFFIX_PTRS 0x10020E90u
#define INDEX       0x1002E818u
#define DISPATCH    0x10022ED0u
#define RULES       0x10093000u
#define PATTERNS    0x1002EFB8u
#define OUTPUTS     0x10021C80u
#define PH_ATTR1    0x10021648u   /* bit 0x80 opens a record group */
#define PH_ATTR2    0x100216C8u

static size_t roff(uint32_t va) { return RDATA_OFF + (va - RDATA_VA); }

static int ru8(const bst_image *img, uint32_t va) {
    size_t o = roff(va);
    return o < img->len ? img->image[o] : 0;
}
static int rs16(const bst_image *img, uint32_t va) {
    size_t o = roff(va);
    return o + 1 < img->len ? (int16_t)(img->image[o] | (img->image[o + 1] << 8)) : 0;
}
static uint32_t ru32(const bst_image *img, uint32_t va) {
    size_t o = roff(va);
    if (o + 3 >= img->len) return 0;
    return (uint32_t)img->image[o] | ((uint32_t)img->image[o + 1] << 8) |
           ((uint32_t)img->image[o + 2] << 16) | ((uint32_t)img->image[o + 3] << 24);
}
static const char *dstr(const bst_image *img, uint32_t va) {
    size_t o = DATA_OFF + (va - DATA_VA);
    return o < img->len ? (const char *)img->image + o : "";
}
static const char *pat_at(const bst_image *img, uint32_t va) {
    size_t o = roff(va);
    return o < img->len ? (const char *)img->image + o : "";
}

static int is_letter(const bst_image *img, int c) {
    return c > 0 && c < 256 && (ru8(img, IS_LETTER + (unsigned)c) & 8);
}
static int lattr(const bst_image *img, int c) {
    return (c > 0 && c < 256) ? ru8(img, LETTER_ATTR + (unsigned)c) : 0;
}

/* Walks one side of a pattern. step is +1 for the right context, -1 for the
   left. */
static int match(const bst_image *img, const char *pat, int pi,
                 const unsigned char *t, int len, int ti, int step) {
    while (pi >= 0 && pat[pi]) {
        char pc = pat[pi];
        if (pc == ')') { pi++; continue; }
        int tc = (ti >= 0 && ti < len) ? t[ti] : 0;

        if (is_letter(img, (unsigned char)pc) || pc == '`') {
            if ((unsigned char)pc != tc) return 0;
            pi += step; ti += step; continue;
        }
        switch (pc) {
        case ' ':
            if (tc && is_letter(img, tc)) return 0;
            pi += step; ti += step; continue;
        case '#':
            if (!(lattr(img, tc) & 1)) return 0;
            pi += step; ti += step; continue;
        case '^':
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 4))) return 0;
            pi += step; ti += step; continue;
        case ':':
            while (tc && is_letter(img, tc) && (lattr(img, tc) & 4)) {
                ti += step;
                tc = (ti >= 0 && ti < len) ? t[ti] : 0;
            }
            pi += step; continue;
        case '+':
            if (tc != 'i' && tc != 'e' && tc != 'y') return 0;
            pi += step; ti += step; continue;
        case '.':
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 2))) return 0;
            pi += step; ti += step; continue;
        case '%': {
            /* Up to three consecutive letters, matched exactly against one of
               the three stored suffixes. They live in .data, not .rdata. */
            char got[4];
            int g = 0, k = ti;
            while (g < 3 && k >= 0 && k < len && is_letter(img, t[k])) got[g++] = (char)t[k++];
            got[g] = 0;
            int hit = 0;
            for (int s = 0; s < 3 && !hit; s++)
                if (strcmp(got, dstr(img, ru32(img, SUFFIX_PTRS + (uint32_t)s * 4))) == 0) hit = 1;
            if (!hit) return 0;
            ti += g - 1;
            pi += step;
            continue;
        }
        case '@':
            if (tc == 'h') {
                int prev = ti > 0 ? t[ti - 1] : 0;
                if (prev != 't' && prev != 'c' && prev != 's') return 0;
                pi--; ti -= 2; continue;
            }
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 0x10))) return 0;
            pi--; ti--; continue;
        default:
            return 0;
        }
    }
    return 1;
}

static int chain_head(const bst_image *img, int key) {
    int bucket = key % 0xED;
    uint32_t o = INDEX + (uint32_t)bucket * 4;
    int entry_off = rs16(img, o);
    int count = ru8(img, o + 2);
    if (entry_off < 0) return -1;
    for (int k = 0; k < count; k++) {
        uint32_t p = DISPATCH + (uint32_t)entry_off * 4 + (uint32_t)k * 4;
        int rkey = rs16(img, p);
        int ridx = rs16(img, p + 2);
        if (rkey == key) return ridx;
        if (rkey < key) break;
    }
    return -1;
}

static void rule_fields(const bst_image *img, int ridx,
                        int *prio, int *next, int *pat, int *out) {
    uint32_t r = RULES + (uint32_t)ridx * 8;
    *prio = rs16(img, r);
    *next = rs16(img, r + 2);
    *pat  = rs16(img, r + 4);
    *out  = rs16(img, r + 6);
}

static int try_at(const bst_image *img, const unsigned char *t, int len, int pos,
                  int *used_out) {
    int best = -1, threshold = 0x240;
    *used_out = 1;

    for (int span = 2; span >= 1; span--) {
        if (pos + span > len) continue;
        if (span == 2 && !(is_letter(img, t[pos + 1]) || t[pos + 1] == 0x60)) continue;
        int key = span == 1 ? t[pos] : (t[pos] | (t[pos + 1] << 8));

        for (int ridx = chain_head(img, key); ridx >= 0;) {
            int prio, next, patoff, outoff;
            rule_fields(img, ridx, &prio, &next, &patoff, &outoff);
            if ((prio & 0xFF) >= threshold) break;

            const char *pat = pat_at(img, PATTERNS + (uint32_t)patoff);
            const char *tp = strchr(pat, 'T');
            const char *cp = strchr(pat, ')');
            if (!tp || !cp) break;
            int ti = (int)(tp - pat), ci = (int)(cp - pat);

            if (match(img, pat, ti + 1, t, len, pos + span, 1) &&
                match(img, pat, ti - 1, t, len, pos - 1, -1)) {
                best = ridx;
                *used_out = span + (ci - ti - 1);
                threshold = prio & 0xFF;
                break;
            }
            ridx = next >= 0 ? next : -1;
        }
    }
    return best;
}

/* Turns phoneme codes into records. A code whose attribute opens a group takes
   three slots, one before it, itself and one after; codes that do not open a
   group are written into whichever of those slots their attributes select. */
typedef struct {
    unsigned char buf[128];
    int pos, before, at, after, stopped;
} builder;

static void build_init(builder *b) {
    memset(b, 0, sizeof *b);
    b->before = b->at = b->after = -1;
}

static void build_emit(const bst_image *img, builder *b, int code) {
    if (b->stopped) return;
    int p = b->pos;
    if (p >= 0x60) { b->stopped = 1; return; }

    int a1 = ru8(img, PH_ATTR1 + (uint32_t)code);
    int a2 = ru8(img, PH_ATTR2 + (uint32_t)code);
    int opens = (a1 & 0x80) || code == 0x4C || code == 0x49;

    if (!opens) {
        int special = code > 0x75 && code < 0x7C;
        if (!(a2 & 2) && !special) {
            if (!(a2 & 4)) {
                p = b->pos;
                b->pos = p + 1;
                b->before = b->at = b->after = -1;
            } else if (b->at == -1) {
                if (b->after == -1) return;
                p = b->after;
                b->after = -1;
            } else {
                p = b->at;
                b->before = b->at = -1;
            }
        } else {
            p = b->before;
            if (p == -1) return;
            b->before = -1;
        }
    } else {
        b->before = p + 1;
        b->at = p + 2;
        b->after = p + 3;
        b->pos = p + 4;
        for (int k = 3; k <= 5; k++)
            if (p + k < (int)sizeof b->buf) b->buf[p + k] = 0;
    }
    if (p + 2 >= 0 && p + 2 < (int)sizeof b->buf) b->buf[p + 2] = (unsigned char)code;
}

/* Puts back the sound of a suffix the normaliser removed. The flag byte is
   walked from the top bit down, and each suffix appends codes chosen by what
   the stem ended on: "churches" gets a different plural from "dogs". */
static void restore_suffix(const bst_image *img, builder *b, int flags, int y_from_i) {
    for (int slot = 0; flags; slot++, flags = (flags << 1) & 0xFF) {
        if (!(flags & 0x80)) continue;
        int last = (b->pos + 1 < (int)sizeof b->buf) ? b->buf[b->pos + 1] : 0;
        int code;

        switch (slot) {
        case 0:                                   /* -ed */
            build_emit(img, b, 0x49);
            if (last == 0x18 || last == 0x14) {
                build_emit(img, b, 0x24);
                build_emit(img, b, 0x76);
                code = 0x14;
            } else if (last != 0 && (last < 4 || last == 0x0D || last == 0x1C ||
                                     last == 0x16 || last == 0x0B)) {
                code = 0x18;
            } else {
                code = 0x14;
            }
            break;
        case 1:                                   /* -ing */
            build_emit(img, b, 0x49);
            build_emit(img, b, 0x29);
            build_emit(img, b, 0x76);
            code = 0x10;
            break;
        case 2:                                   /* -ly */
            if (last == 0x11) {
                code = 0x49;
            } else {
                /* When the stem's i became a y, a preceding vowel marker is
                   promoted before the suffix goes on. */
                if (y_from_i && b->pos >= 2 && b->buf[b->pos - 2] == 0x23)
                    b->buf[b->pos - 2] = 0x24;
                build_emit(img, b, 0x49);
                code = 0x11;
            }
            build_emit(img, b, code);
            build_emit(img, b, 0x23);
            code = 0x76;
            break;
        case 3:                                   /* -s */
        case 4:                                   /* -'s */
            build_emit(img, b, 0x49);
            if (last < 0x0E && ((1u << (last & 0x1F)) & 0x38C8u)) {
                build_emit(img, b, 0x29);
                build_emit(img, b, 0x76);
                code = 6;
            } else if (last < 0x1D && (last > 0x15 || last == 1 || last == 2)) {
                code = 0x0D;
            } else {
                code = 6;
            }
            break;
        default:
            return;
        }
        build_emit(img, b, code);
    }
}

void bst_lts(const bst_image *img, const bst_word *w, bst_stream *out) {
    const unsigned char *t = (const unsigned char *)w->buf;
    int len = w->len;
    builder b;
    build_init(&b);

    int pos = 1;
    while (pos < len && t[pos] != '_' && t[pos]) {
        int used = 1;
        int ridx = try_at(img, t, len, pos, &used);
        if (ridx < 0) { pos++; continue; }

        int prio, next, patoff, outoff;
        rule_fields(img, ridx, &prio, &next, &patoff, &outoff);
        const char *o = pat_at(img, OUTPUTS + (uint32_t)outoff);
        for (; *o; o++) build_emit(img, &b, (unsigned char)*o);
        pos += used > 0 ? used : 1;
    }

    if (w->flags) restore_suffix(img, &b, w->flags, w->y_from_i);

    memset(out, 0, sizeof *out);
    int n = b.pos + 2;
    if (n > BST_STREAM_MAX - 1) n = BST_STREAM_MAX - 1;
    for (int i = 1; i <= n; i++) out->buf[i - 1] = b.buf[i];
    out->len = n;
}
