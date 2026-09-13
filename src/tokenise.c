#include <string.h>
#include "bst_token.h"

/* See bst_token.h. The transition table and every string the handlers emit
   are read from the image, so the only thing here is the machine that walks
   them. */

#define TABLE   0x10021748u   /* state, handler, next state; twelve bytes each */
#define CHATTR  0x10021020u   /* per character: punctuation, digit, letter, ... */
#define CASEMAP 0x10021220u

/* The handlers, by the address the table names them at. */
#define H_LETTER   0x10009800u
#define H_DIGIT    0x10009810u
#define H_EAT1     0x10009840u
#define H_SPACE    0x10009850u
#define H_DOT      0x10009870u
#define H_CURRENCY 0x10009890u
#define H_PUNCT    0x100098b0u
#define H_DASH     0x100098d0u
#define H_EXPONENT 0x10009AA0u
#define H_MODE1    0x10009AD0u
#define H_MODE2    0x10009AE0u
#define H_DEL      0x10009AF0u
#define H_OPENER   0x10009B10u
#define H_TILDE    0x10009B70u
#define H_APOSDOT  0x10009BD0u
#define H_APOS     0x10009BF0u
#define H_POSSESS  0x10009C10u
#define H_EAT2     0x10007030u
#define H_EAT3     0x10007040u
#define H_WORD     0x10007AC0u
#define H_PUNCTOUT 0x100070A0u
#define H_DOTOUT   0x100074F0u

/* The tail the reader adds after the text, which is what closes the last
   sentence whether or not the text ends in a full stop. */
static const uint8_t TAIL[4] = { ' ', '\n', '}', ' ' };

static int u8at(const bst_image *img, unsigned va, unsigned i) {
    const uint8_t *p = bst_at(img, va + i, 1);
    return p ? *p : 0;
}

static int chattr(const bst_tok *t, int c) { return u8at(t->img, CHATTR, c & 0xFF); }
static int is_letter(const bst_tok *t, int c) { return (chattr(t, c) & 8) != 0; }
static int is_digit(const bst_tok *t, int c)  { return (chattr(t, c) & 4) != 0; }
static int is_punct(const bst_tok *t, int c)  { return (chattr(t, c) & 2) != 0; }
static int is_upper(const bst_tok *t, int c)  { return (chattr(t, c) & 0x20) != 0; }
static int lower(const bst_tok *t, int c) { return u8at(t->img, CASEMAP, c & 0xFF); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' ||
                                    c == '\r' || c == '\v' || c == '\f'; }

/* ---- input ------------------------------------------------------------- */

static int source(bst_tok *t) {
    if (t->tp < t->tn) return t->text[t->tp++];
    if (t->tail < (int)(sizeof TAIL)) return TAIL[t->tail++];
    return 0xFFFF;
}

static int rd(bst_tok *t) {
    if (t->push) {
        t->push--;
        t->cur++;
        return t->ring[t->cur] == 0xFF ? 0xFFFF : t->ring[t->cur];
    }
    int c = source(t);
    if (c == 0xFFFF) {
        if (t->cur + 1 < BST_TOK_RING) t->ring[++t->cur] = 0xFF;
        return 0xFFFF;
    }
    if (c == 0x5C) {
        int d = source(t);
        if (d == 0x7E) c = 0x7E;
        else if (d != 0x5C && d != 0xFFFF) { if (t->tp > 0) t->tp--; else t->tail--; }
    }
    if (t->cur + 1 < BST_TOK_RING) t->ring[++t->cur] = (uint8_t)c;
    if (t->cur + 1 > t->nring) t->nring = t->cur + 1;
    return c;
}

static void unread(bst_tok *t, int n) {
    t->push += n;
    t->cur -= n;
    if (t->cur < -1) { t->cur = -1; t->push = 0; }
}

/* ---- output ------------------------------------------------------------ */

static void emit(bst_tok *t, int c) {
    if (c == ' ' && !t->textmode && t->lastout == ' ') return;
    if (t->overflow) return;
    t->lastout = c;
    if (t->nout >= BST_TOK_OUT) { t->overflow = 1; t->nout = 0; return; }
    t->out[t->nout++] = (uint8_t)c;
}

/* A run of phoneme codes, bracketed by the two mode markers. */
static void emit_codes(bst_tok *t, unsigned va) {
    const uint8_t *p = bst_at(t->img, va, 1);
    if (!p || !*p) return;
    emit(t, ' ');
    emit(t, 0xFE);
    for (; *p; p++) {
        if (*p == '|') {
            for (int k = 0; k < 6 && *p; k++, p++) emit(t, *p);
            p--;
        } else emit(t, *p);
    }
    emit(t, 0xFF);
    emit(t, ' ');
}

/* Several handlers say something outright rather than spelling it: the
   pointer names a stored code string. Only the ones plain text reaches are
   wired up so far. */
static void emit_ptr(bst_tok *t, unsigned ptrva) {
    const uint8_t *e = bst_at(t->img, ptrva, 4);
    if (!e) return;
    uint32_t va = (uint32_t)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
    if (va) emit_codes(t, va);
}

/* ---- the transition table ---------------------------------------------- */

static int row(const bst_tok *t, int i, int *state, unsigned *handler, int *next) {
    const uint8_t *p = bst_at(t->img, TABLE + (unsigned)i * 12, 12);
    if (!p) return 0;
    uint32_t h = (uint32_t)(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    if (h < 0x10001000u || h >= 0x10019000u) return 0;
    *state = p[0];
    *handler = h;
    *next = p[8];
    return 1;
}

/* ---- the handlers ------------------------------------------------------ */

static void word_out(bst_tok *t);

/* Punctuation that closes a phrase versus punctuation that only groups. */
static int closes(int b) {
    switch (b) {
    case '"': case 0xAE: case '\'': case '{': case '[': case '(':
    case '`': case ',':  case ';':   case ':': case '.': case '!':
    case '?': case 0xAF: case '}':   case ']': case ')': case '-':
        return 1;
    default:
        return 0;
    }
}

static void punct_out(bst_tok *t, int c) {
    int b = t->ring[t->start];
    t->prevkind = t->kind;
    t->kind = 3;
    unread(t, 1);
    t->quote = (b == '"' || b == 0xAE || b == '\'' || b == '{' ||
                b == '[' || b == '(' || b == '`');
    t->sentence = !closes(b);
    if (t->literal) return;
    if (b == ':' && is_digit(t, t->prevch) && is_digit(t, c)) { emit(t, ','); return; }
    if (!t->sentence && b != '\'' && b != '`') { emit(t, b); return; }
    if (b == '-') { emit(t, ','); return; }
}

static void dot_out(bst_tok *t, int c) {
    (void)emit_ptr;
    t->sentence = 0;
    t->prevkind = t->kind;
    t->kind = 3;
    unread(t, 1);
    if (t->literal) { emit(t, '.'); return; }

    /* A full stop that something other than a space follows is part of a
       token rather than the end of one. */
    int n = 0, d;
    do {
        do { n++; d = rd(t); } while (d == '"');
    } while (d == 0xAF || d == '\'' || d == '}' || d == ']' || d == ')');
    unread(t, n);
    if (!is_space(d) && d != 0xFFFF && d != 0x7F) return;
    emit(t, '.');
}

static int handler(bst_tok *t, unsigned h, int c) {
    switch (h) {
    case H_LETTER:   return is_letter(t, c);
    case H_DIGIT:    return is_digit(t, c);
    case H_SPACE:    return is_space(c);
    case H_DOT:      return c == 0x2E;
    case H_DASH:     return c == 0x2D;
    case H_TILDE:    return c == 0x7E;
    case H_APOS:     return c == 0x27;
    case H_APOSDOT:  return c == 0x27 || c == 0x2E || c == 0x7E;
    case H_CURRENCY: return c == 0x24 || c == 0x9C || c == 0xBE;
    case H_PUNCT:    return is_punct(t, c);
    case H_MODE1:    return 0;
    case H_MODE2:    return 0;
    case H_EXPONENT: return 0;
    case H_OPENER:   return 0;
    case H_POSSESS:  return 0;
    case H_DEL:      if (c == 0x7F) { unread(t, 1); return 1; } return 0;
    case H_EAT1:     unread(t, 1); return 1;
    case H_EAT2:     unread(t, 2); return 1;
    case H_EAT3:     unread(t, 3); return 1;
    case H_PUNCTOUT: punct_out(t, c); return 1;
    case H_DOTOUT:   dot_out(t, c); return 1;
    case H_WORD:     unread(t, 1); word_out(t); return 1;
    default:         return 0;
    }
}

/* The word the machine has just delimited, copied into the scratch buffer
   with its case folded. */
static void word_out(bst_tok *t) {
    for (int i = t->start; i <= t->cur; i++) {
        int c = t->ring[i];
        emit(t, is_upper(t, c) ? lower(t, c) : c);
    }
    emit(t, ' ');
    t->prevkind = t->kind;
    t->kind = 5 - ((chattr(t, t->ring[t->start]) & 0x20) == 0);
}

/* ---- the machine ------------------------------------------------------- */

/* The first row for a state. The rows for a state are contiguous, and once
   inside one the machine walks forward without looking at the state column
   again, so a state with no match falls into the next state's rows. */
static int first_row(const bst_tok *t, int state) {
    int i = 0, st, nx;
    unsigned h;
    int prev = -1;
    while (row(t, i, &st, &h, &nx)) {
        if (st != prev) {
            if (st == state) return i;
            prev = st;
        }
        i++;
    }
    return -1;
}

/* Runs until the machine returns to state zero, which is one unit of text. */
static int scan(bst_tok *t) {
    t->overflow = 0;
    t->state = 0;
    for (int guard = 0; guard < 100000; guard++) {
        int c = rd(t);
        if (c == 0xFFFF) return 0;

        int i = first_row(t, t->state);
        if (i < 0) return 0;
        int st, nx;
        unsigned h;
        while (row(t, i, &st, &h, &nx)) {
            if (handler(t, h, c)) break;
            i++;
        }
        if (!row(t, i, &st, &h, &nx)) return 0;
        t->state = nx;
        if (t->state == 0) {
            t->start = t->cur + 1;
            return 1;
        }
    }
    return 0;
}

/* ---- the classifier ----------------------------------------------------- */

/* Reads the scratch buffer a byte at a time, refilling it from the machine,
   and says what kind of thing each byte is. */
static int classify(bst_tok *t, int *cls) {
    for (;;) {
        while (t->pos < t->nout) {
            int b = t->out[t->pos++];
            if (t->raw) { t->raw--; *cls = 0; return b; }
            if (b == 0xFE) { t->textmode = 0; continue; }
            if (b == 0xFF) { t->textmode = 1; *cls = 0x13; return 0x20; }
            if (t->textmode) {
                if (is_letter(t, b) || b == 0x27) *cls = 0x111;
                else if (b == 0x20)               *cls = 0x13;
                else                              *cls = 0x112;
                return b;
            }
            if ((b != 0 && b < 0x31) || (b > 0x4E && b < 0x5C)) { *cls = 0x124; return b; }
            if (b < 0x39) { *cls = 0x25;  return b; }
            if (b < 0x49) { *cls = 0x68;  return b; }
            if (b < 0x4F) { *cls = 0x146; return b; }
            if (b == 0x5C) { *cls = 0x47; return b; }
            if (b == 0x7C) { *cls = 0x189; t->raw = 5; return b; }
        }
        if (t->ended) { *cls = 0x101; return 0xFFFF; }
        t->pos = 0;
        t->nout = 0;
        if (!scan(t)) t->ended = 1;
    }
}

/* ---- the front ---------------------------------------------------------- */

void bst_tok_init(bst_tok *t, const bst_image *img, const char *text) {
    memset(t, 0, sizeof *t);
    t->img = img;
    t->text = (const uint8_t *)text;
    t->tn = (int)strlen(text);
    t->cur = -1;
    t->start = 0;
    t->lastout = ' ';
    t->textmode = 1;
}

int bst_tok_next(bst_tok *t, uint8_t *buf) {
    for (;;) {
        int cls;
        int b = classify(t, &cls);
        if (!(cls & 0x100)) continue;
        switch (cls) {
        case 0x101:
            return 6;
        case 0x111: {
            /* A run of letters and apostrophes: one word. */
            int n = 0;
            do {
                if (n < 99) buf[n + 1] = (uint8_t)b;
                n++;
                b = classify(t, &cls);
            } while (b != 0xFFFF && cls == 0x111);
            if (b != 0xFFFF) t->pos--;
            buf[n + 1] = 0;
            buf[0] = 0;
            return 2;
        }
        case 0x112:
            buf[0] = (uint8_t)b;
            return 4;
        case 0x124: {
            /* A run of phoneme codes the machine wrote outright. */
            bst_builder w;
            bst_build_init(&w);
            do {
                bst_build_emit(t->img, &w, b);
                b = classify(t, &cls);
            } while (b != 0xFFFF && (cls & 0x20));
            if (b != 0xFFFF) t->pos--;
            buf[0] = (uint8_t)w.pos;
            memcpy(buf + 1, w.buf + 1, (size_t)w.pos + 1);
            return 3;
        }
        case 0x146:
            buf[0] = (uint8_t)b;
            return 5;
        case 0x189:
            for (int i = 0; i < 6; i++) {
                buf[i] = (uint8_t)b;
                if (i < 5) b = classify(t, &cls);
            }
            return 1;
        default:
            break;
        }
    }
}
