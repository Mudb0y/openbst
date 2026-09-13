#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

/* Per-build addresses of the engine internals the oracle drives directly.
   Everything here was recovered from disassembly and is specific to one
   binary, so a new build needs a new entry rather than an edit to an old one. */
typedef struct {
    const char *name;
    uint32_t    image_size;     /* identifies the build */

    /* Diagnostic text buffer: base pointer, write index, capacity. The engine
       appends to it through a single routine, and both the phoneme dump and
       the frame dump go through that routine. */
    uint32_t phbuf_ptr;
    uint32_t phbuf_idx;
    uint32_t phbuf_cap;

    /* Bit 0 enables the 16-byte-per-frame synthesizer parameter dump; bit 6
       suppresses it again. */
    uint32_t frame_flags;

    /* Instruction immediately after the append routine increments its write
       index; where the buffer can safely be drained and rewound. */
    uint32_t phbuf_after_inc;

    /* Acoustic target table: ten signed bytes per entry, addressed as
       base + (voice * 410 + phoneme * 10 + variant) * 10. The engine doubles
       each byte into a Q8 reflection coefficient. Voice is validated to 1..6.
       Steady frames are copied from here; transitions are interpolated. */
    uint32_t targets;
    uint32_t voice_sel;

    /* Entry of the routine that receives each segment record and fetches its
       acoustic targets; hooking it yields the input to frame generation. */
    uint32_t seg_entry;

    /* Frame builder: entry point, the current target vector and running
       parameter state (ten int16 each), this frame's duration and the time
       remaining in the current transition. */
    /* Sampled at the interpolation step, not the builder's entry: the frame
       duration is recomputed in between. */
    uint32_t build_entry;
    uint32_t targets_cur;
    uint32_t state_cur;
    uint32_t frame_dur;
    uint32_t trans_left;
    uint32_t prev_targets;   /* target vector of the previous segment */
    uint32_t fresh_flag;     /* set when a new segment was just fetched */
    uint32_t build_done;     /* join point after the interpolation step */

    /* Gain: computed by a routine called from the frame builder, smoothed the
       same way the coefficients are and written to frame byte 2. */
    uint32_t gain_before;    /* the call site */
    uint32_t gain_after;     /* after the result is stored into the frame */
    uint32_t gain_state;     /* fixed-point accumulator */
    uint32_t gain_target;
    uint32_t gain_base;
    uint32_t gain_mode_adj;
    uint32_t gain_clock;     /* time base the smoothing rate divides by */
    uint32_t exc_class;      /* excitation class, selects the mode adjustment */
    uint32_t frame_gain;     /* frame byte 2 */

    /* Pitch: an accumulator smoothed toward a target on its own clock, whose
       high byte is the period in samples that frame byte 3 carries. */
    uint32_t pitch_before;
    uint32_t pitch_after;
    uint32_t pitch_acc;
    uint32_t pitch_target;
    uint32_t pitch_clock;
    uint32_t pitch_period;

    /* Verbosity passed to GetPhBuf. Output is gated on the magnitude of a
       counter derived from it exceeding five, so anything at or below five
       yields nothing at all. */
    int default_level;
} profile;

static const profile profiles[] = {
    {
        .name          = "b32-1995",
        .image_size    = 0x9f000,
        .phbuf_ptr     = 0x1009627c,
        .phbuf_idx     = 0x10096280,
        .phbuf_cap     = 0x10096284,
        .frame_flags   = 0x1001c020,
        .phbuf_after_inc = 0x1000bbdb,
        .targets       = 0x10022284,
        .voice_sel     = 0x10019a5a,
        .seg_entry     = 0x10003900,
        .build_entry   = 0x1000893a,
        .targets_cur   = 0x100192f0,
        .state_cur     = 0x10019310,
        .frame_dur     = 0x1001b424,
        .trans_left    = 0x1001c44a,
        .prev_targets  = 0x1001b780,
        .fresh_flag    = 0x1001e3bc,
        .build_done    = 0x10008cae,
        .gain_before   = 0x10008d54,
        .gain_after    = 0x10008d5e,
        .gain_state    = 0x10019a5c,
        .gain_target   = 0x10019a84,
        .gain_base     = 0x10019398,
        .gain_mode_adj = 0x1001939c,
        .gain_clock    = 0x1001c446,
        .exc_class     = 0x1001e644,
        .frame_gain    = 0x10019a32,
        .pitch_before  = 0x10008d5e,
        .pitch_after   = 0x10008d63,
        .pitch_acc     = 0x100192e0,
        .pitch_target  = 0x1001c014,
        .pitch_clock   = 0x1001e3ac,
        .pitch_period  = 0x1001b46a,
        .default_level = 6,
    },
};

#define NPROFILES ((int)(sizeof profiles / sizeof profiles[0]))

#endif
