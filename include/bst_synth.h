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

/* Tables lifted from the original binary. The log and antilog pair are
   base two with 32 units to the octave, and the engine uses them to multiply
   and divide without a multiply: add logs, take the antilog. */
typedef struct {
    int16_t pulse[BST_PULSE_LEN];
    int16_t noise[BST_NOISE_BYTES / 2];
    int16_t gain[BST_GAIN_ENTRIES];
    int16_t log[256];
    int16_t alog[256];
} bst_tables;

/* Parameter interpolator. The engine never jumps to a target: each frame it
   moves the running state a fraction of the remaining distance, the fraction
   being this frame's duration over the time left in the transition. Working
   in the log domain is what makes the result awkward to reproduce by fitting
   a curve, and exact to reproduce by following the arithmetic. */
typedef struct {
    const bst_tables *t;
    int16_t state[BST_ORDER];   /* twice the Q8 coefficient */
} bst_interp;

void bst_interp_init(bst_interp *ip, const bst_tables *t);
void bst_interp_snap(bst_interp *ip, const int16_t target[BST_ORDER]);
void bst_interp_step(bst_interp *ip, const int16_t target[BST_ORDER],
                     int dur, int remaining);

/* Minimum length of a transition, from the total coefficient movement across
   it: three eighths of the summed absolute difference. */
int  bst_transition_len(const int16_t from[BST_ORDER], const int16_t to[BST_ORDER]);

/* Gain, smoothed the same way but on its own clock. The accumulator carries
   eight fractional bits and the emitted frame byte is its integer part plus a
   base and an adjustment that depends on the excitation class. */
typedef struct {
    const bst_tables *t;
    int16_t acc;
} bst_gain;

void bst_gain_init(bst_gain *g, const bst_tables *t);
int  bst_gain_value(const bst_gain *g, int base, int adj, int exc_class);
void bst_gain_step(bst_gain *g, int target, int dur, int clock);

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
