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

    uint8_t  out[BST_TOK_OUT];
    int      nout, lastout, overflow;

    int      state, kind, prevkind, prevch;
    int      textmode;        /* which of the two the classifier is reading */
    int      quote, spell, literal, sentence;

    /* The classifier's position in the scratch buffer. */
    int      pos, raw, ended;
} bst_tok;

void bst_tok_init(bst_tok *t, const bst_image *img, const char *text);

/* Fills buf with the next token and returns its kind: 1 a command, 3 a word,
   4 punctuation, 5 a marker, 6 the end of the text. */
int  bst_tok_next(bst_tok *t, uint8_t *buf);

#endif
