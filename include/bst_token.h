#ifndef BST_TOKEN_H
#define BST_TOKEN_H

#include <stdint.h>
#include "bst_text.h"

/* The tokeniser: text in, the tokens the assembler consumes out.
 *
 * Between the two sits a character state machine whose transition table lives
 * in the image: fifty-three states, a hundred and eleven rows of (state,
 * test, next state). The tests are small predicates -- is this a letter, a
 * digit, a full stop, an opening quote -- and some of them are also actions,
 * emitting into a scratch buffer and consuming input. The machine runs until
 * it returns to state zero, at which point the scratch buffer holds one
 * token's worth of text, and the classifier turns that into the token the
 * front end sees.
 *
 * The scratch buffer is a mixture of text and phoneme codes: a handler that
 * knows how to say something outright, a full stop or a currency sign, writes
 * the codes in and brackets them with the two markers that switch the
 * classifier between reading text and reading codes. */

#define BST_TOK_RING 0x2000
#define BST_TOK_OUT  0x800

typedef struct {
    const bst_image *img;

    const uint8_t *text;
    int      tn, tp;          /* the source and how much of it has been read */
    int      tail;            /* how much of the synthetic tail has been used */

    uint8_t  ring[BST_TOK_RING];
    int      nring, cur, start, push;
    int      realend;    /* the last ring position that came from the text */

    uint8_t  out[BST_TOK_OUT];
    int      nout, lastout, overflow;

    int      state, kind, prevkind, prevch;
    int      textmode;        /* which of the two the classifier is reading */
    int      quote, spell, literal, sentence;

    /* The classifier's position in the scratch buffer. */
    int      pos, raw, ended;

    /* The token ring proper -- the input ring above is characters. Tokens
       are read ahead of the assembler so that a sentence too long to say in
       one breath can have a break put into it before any of it is spoken. */
    struct {
        uint8_t  type, flag, spare, pushback;
        uint8_t  len;
        uint8_t  buf[104];
    } tok[20];
    int      rd, wr, held, blocked, bytes;
    int      total;      /* the text accumulated since the last break */
    int      breath;     /* how long a breath group may be */
    int      window;     /* how far back the splitter looks */
    int      run;        /* how many words since the last sentence end */
    int      quest;      /* a spelled-out letter has been seen in this run */
    int      eat;        /* the exception entry swallows the stop after it */
} bst_tok;

/* The reader, shared with the exception engine's lookahead. */
int  bst_tok_read(bst_tok *t);
int  bst_tok_peek_read(bst_tok *t);
void bst_tok_unread(bst_tok *t, int n);

/* Says a stored string of phoneme codes named by a pointer in the image. */
void bst_tok_say(bst_tok *t, unsigned ptrva);

/* Says a run of digits. */
void bst_say_number(bst_tok *t, const uint8_t *digits, int n);

/* Says a run of digits one at a time. */
void bst_say_digits(bst_tok *t, const uint8_t *digits, int n);

/* Looks a word up in the exception table. Returns the number of phoneme
   codes written, or zero if the table does not hold it. */
int  bst_except(bst_tok *t, const uint8_t *word, int wlen,
                uint8_t *out, int max);

void bst_tok_init(bst_tok *t, const bst_image *img, const char *text);

/* Fills buf with the next token and returns its kind: 1 a command, 3 a word,
   4 punctuation, 5 a marker, 6 the end of the text. */
int  bst_tok_next(bst_tok *t, uint8_t *buf);

#endif
