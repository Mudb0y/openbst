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
        .default_level = 6,
    },
};

#define NPROFILES ((int)(sizeof profiles / sizeof profiles[0]))

#endif
