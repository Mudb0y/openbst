#include <string.h>
#include "bst_token.h"

/* The exception engine: stored pronunciations for the words the rules would
   get wrong.
 *
 * It sits in front of the character state machine and, for every word, walks
 * a trie in the image one letter at a time, remembering the longest entry
 * that matched. A hit is a record in a pool: a string of phoneme codes whose
 * last byte carries the end bit, with 0x7F escaping a byte that would
 * otherwise look like the end. The record can hold several alternatives
 * separated by runs of condition bytes, and the first alternative whose
 * conditions hold is the one used, which is how one entry covers "read" in
 * both tenses.
 *
 * This is where "the", "about" and a hundred other common words get their
 * pronunciation; they never reach the dictionary or the letter-to-sound rules
 * at all. */

#define DESC    0x10030898u   /* the table descriptor */
#define SYMMAP  0x10021320u   /* character to trie symbol */
#define CHATTR  0x10021020u

#define COND_LO 0x5D
#define COND_HI 0x72

typedef struct {
    const bst_image *img;
    const uint8_t   *states, *links, *pool;
    int              base, max;
} trie;

static int load_trie(const bst_image *img, trie *w) {
    const uint8_t *d = bst_at(img, DESC, 20);
    if (!d) return 0;
    uint32_t st = (uint32_t)(d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24));
    uint32_t li = (uint32_t)(d[8] | (d[9] << 8) | (d[10] << 16) | (d[11] << 24));
    uint32_t po = (uint32_t)(d[16] | (d[17] << 8) | (d[18] << 16) | (d[19] << 24));
    w->img = img;
    w->base = d[12] | (d[13] << 8);
    w->max  = d[14] | (d[15] << 8);
    w->states = bst_at(img, st, 4);
    w->links  = bst_at(img, li, 4);
    w->pool   = bst_at(img, po, 1);
    return w->states && w->pool;
}

static int symbol(const bst_image *img, int c) {
    const uint8_t *p = bst_at(img, SYMMAP + (unsigned)(c & 0xFF), 1);
    return p ? *p : 0;
}

static int u16at(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* One step of the walk. `cur` is the node reached so far, or -1 to start.
   Returns the output at the new node, sets *more when the node has children,
   and -1 on a dead end. */
static int step(const trie *w, int *cur, int c, int *more) {
    int sym = symbol(w->img, c);
    *more = 0;
    if (!sym) return -1;
    int st;
    if (*cur < 0) st = w->base;
    else {
        const uint8_t *e = w->states + (size_t)*cur * 4;
        int v = u16at(e);
        if (v & 0x8000) {
            int sv = v - 0x10000;
            st = u16at(w->links + (ptrdiff_t)sv * 4);
        } else {
            if (!(e[2] & 0x80)) return -1;
            st = v;
        }
    }
    int n = st + sym;
    if (n < 0 || n > w->max) return -1;
    const uint8_t *e = w->states + (size_t)n * 4;
    if ((e[2] & 0x7F) != sym) return -1;
    *cur = n;
    if (e[1] & 0x80) {
        int v = u16at(e);
        int sv = v >= 0x8000 ? v - 0x10000 : v;
        *more = 1;
        return u16at(w->links + (ptrdiff_t)sv * 4 + 2);
    }
    if (e[2] & 0x80) { *more = 1; return 0; }
    return u16at(e);
}

/* Copies a pool record out, undoing the escape and clearing the end bit. */
static int record(const trie *w, int at, uint8_t *out, int max) {
    const uint8_t *p = w->pool + at;
    int n = 0, last;
    do {
        int b = *p;
        if (b == 0x7F) { p++; b = (*p + 0x7E) & 0xFF; }
        last = *p;
        if (n < max) out[n] = (uint8_t)(b & 0x7F);
        n++;
        p++;
    } while (!(last & 0x80) && n < max);
    return n;
}

/* ---- the conditions ---------------------------------------------------- */

/* The lookahead the conditions read: it runs past the end of what has been
   consumed, pulling new input in and remembering how much to give back. */
typedef struct { bst_tok *t; int at, pulled; } peek;

static void peek_init(peek *p, bst_tok *t) { p->t = t; p->at = t->cur; p->pulled = 0; }

static int peek_next(peek *p) {
    int c;
    do {
        if (p->at < p->t->cur) { p->at++; c = p->t->ring[p->at]; }
        else { p->at++; c = bst_tok_peek_read(p->t); if (c == 0xFFFF) { p->at--; return c; } p->pulled++; }
    } while (c == 0x7F);
    return c;
}

static void peek_done(peek *p) { if (p->pulled) bst_tok_unread(p->t, p->pulled); }

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' ||
                                    c == '\r' || c == '\v' || c == '\f'; }

static int chattr(const bst_tok *t, int c) {
    const uint8_t *p = bst_at(t->img, CHATTR + (unsigned)(c & 0xFF), 1);
    return p ? *p : 0;
}

/* Each opcode asks something about the word's surroundings: how it is
   capitalised, what follows it, whether it is one of a handful of things the
   host can turn on. The ones a plain sentence reaches are answered; the rest,
   which ask about numbers and dates, are false. */
static int condition(bst_tok *t, int op, const uint8_t *word, int wlen) {
    peek p;
    int r = 0, c;
    switch (op - COND_LO) {
    case 0:                                   /* starts lower case */
        return (chattr(t, word[0]) & 0x20) == 0;
    case 1: {                                 /* the stop after an abbreviation */
        peek_init(&p, t);
        c = peek_next(&p);
        if (c == '-')      { peek_done(&p); return 0; }
        if (c != '.')      { peek_done(&p); return 1; }
        do {
            do { c = peek_next(&p); } while (c == '"');
        } while (c == 0xAF || c == '\'' || c == '}' || c == ']' || c == ')');
        if (!is_space(c)) { peek_done(&p); return 1; }
        if (c == ' ') {
            int c2 = peek_next(&p);
            if (c2 == '.') {
                int c3 = peek_next(&p);
                if (!(c3 == ' ' && peek_next(&p) == '.')) t->eat = 1;
            } else if (c2 != ' ') {
                t->eat = 1;
            }
        } else if (c == '\n') {
            if (chattr(t, peek_next(&p)) & 2) t->eat = 1;
        }
        peek_done(&p);
        return 1;
    }
    case 2:                                   /* a full stop follows */
        peek_init(&p, t);
        r = peek_next(&p) == '.';
        peek_done(&p);
        return r;
    case 3:                                   /* a full stop the entry eats */
        peek_init(&p, t);
        if (peek_next(&p) == '.') {
            c = peek_next(&p);
            if (is_space(c)) t->eat = 1;
        }
        peek_done(&p);
        return 1;
    case 5: {                                 /* an optional stop, then a capital */
        int dot;
        peek_init(&p, t);
        c = peek_next(&p);
        dot = (c == '.');
        if (dot) c = peek_next(&p);
        r = 0;
        if (c == ' ') {
            c = peek_next(&p);
            r = (chattr(t, c) & 0x20) != 0;
        }
        peek_done(&p);
        if (r) t->eat = dot;
        return r;
    }
    case 6:                                   /* starts upper case */
        return (chattr(t, word[0]) & 0x20) != 0;
    case 7:                                   /* punctuation follows */
        peek_init(&p, t);
        r = (chattr(t, peek_next(&p)) & 2) != 0;
        peek_done(&p);
        return r;
    case 8:                                   /* no lower-case letter in it */
        for (int i = 0; i < wlen; i++) {
            if (chattr(t, word[i]) & 0x20) continue;
            if (chattr(t, word[i]) & 8) return 0;
        }
        return 1;
    case 9:
        return t->kind == 2 && t->prevkind == 5;
    default:
        return 0;
    }
}

/* Picks the alternative whose conditions hold and returns its extent. */
static void choose(bst_tok *t, uint8_t *rec, int n, const uint8_t *word,
                   int wlen, int *from, int *to) {
    int start = 0, i = 0;
    *from = 0;
    *to = n - 1;
    while (i < n) {
        if (rec[i] < COND_LO || rec[i] > COND_HI) { i++; continue; }
        int end = i - 1;
        int ok = 1;
        while (i < n && rec[i] >= COND_LO && rec[i] <= COND_HI) {
            /* The engine stops at the first condition that fails, and some
               of them have side effects, so stopping matters. */
            if (!condition(t, rec[i], word, wlen)) { ok = 0; i++; break; }
            i++;
        }
        while (i < n && rec[i] >= COND_LO && rec[i] <= COND_HI) i++;
        if (ok) { *from = start; *to = end; return; }
        start = i;
    }
    *from = start;
    *to = n - 1;
}

/* ---- the front --------------------------------------------------------- */

int bst_except(bst_tok *t, const uint8_t *word, int wlen,
               uint8_t *out, int max) {
    trie w;
    if (!load_trie(t->img, &w)) return 0;
    if (wlen <= 0) return 0;

    int cur = -1, best = 0, bestlen = 0;
    for (int i = 0; i < wlen; i++) {
        int more;
        int r = step(&w, &cur, word[i], &more);
        if (r < 0) break;
        if (r != 0) { best = r; bestlen = i + 1; }
        if (!more) break;
    }
    if (!best || bestlen != wlen) return 0;

    uint8_t rec[256];
    int n = record(&w, best, rec, (int)sizeof rec);
    int from, to;
    t->eat = 0;
    choose(t, rec, n, word, wlen, &from, &to);
    /* An entry that read a full stop as part of itself takes it. */
    if (t->eat) { bst_tok_read(t); t->eat = 0; }
    int m = 0;
    for (int i = from; i <= to && m < max; i++) out[m++] = rec[i];
    return m;
}
