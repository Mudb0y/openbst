#ifndef BST_SYNTH_H
#define BST_SYNTH_H

#include <stdint.h>
#include <stddef.h>

/* The BeSTspeech low-level synthesizer: a 10th-order LPC lattice driven by a
   stored glottal pulse and a table of noise values, all in Q8 fixed point.
   Frames arrive as 16 opaque bytes and each one is held for a whole number of
   pitch periods, so frame duration is pitch-synchronous rather than fixed. */

#define BST_ORDER        10
#define BST_PULSE_LEN    160   /* phase units, 16 per output sample */
#define BST_NOISE_BYTES   64
#define BST_GAIN_ENTRIES 256

/* Tables lifted from the original binary. */
typedef struct {
    int16_t pulse[BST_PULSE_LEN];
    int16_t noise[BST_NOISE_BYTES / 2];
    int16_t gain[BST_GAIN_ENTRIES];
} bst_tables;

typedef struct {
    const bst_tables *t;

    int32_t  b[BST_ORDER];        /* lattice backward states */
    int16_t  k[BST_ORDER];        /* reflection coefficients, Q8 */

    int16_t  gain;
    uint16_t period;              /* Q4: 16 phase units per output sample */
    uint16_t phase;
    uint16_t rand;                /* excitation noise generator */
    uint8_t  mode;                /* 0x00 silence, 0x10 voiced,
                                     0x20 noise, 0x30 mixed */
    int      periods_left;
} bst_synth;

/* Excitation modes, as held in the top nibble of frame byte 0. */
#define BST_SILENT 0x00
#define BST_VOICED 0x10
#define BST_NOISE  0x20
#define BST_MIXED  0x30

void   bst_synth_init(bst_synth *s, const bst_tables *t);

/* Loads one 16-byte frame. Returns 0 for an end marker (byte 0 of 0x00 or
   0xff), 1 otherwise. */
int    bst_synth_frame(bst_synth *s, const uint8_t f[16]);

/* Renders the current frame to completion, appending to out. Returns the
   number of samples written, capped at max. */
size_t bst_synth_run(bst_synth *s, int16_t *out, size_t max);

/* Reads the three tables out of a loaded BeSTspeech image. */
int    bst_tables_load(bst_tables *t, const void *image, size_t len);

#endif
