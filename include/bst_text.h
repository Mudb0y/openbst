#ifndef BST_TEXT_H
#define BST_TEXT_H

#include <stdint.h>
#include <stddef.h>

/* The word front end: normalisation, dictionary lookup and letter-to-sound.
   Everything here reads its tables out of a loaded BeSTspeech image rather
   than embedding them, so the library carries no lifted data of its own. */

#define BST_SECTIONS 16

typedef struct {
    const uint8_t *image;
    size_t         len;
    /* The section map, so an address in the image can be followed wherever it
       points. Most tables live in .rdata, but the pronunciation modifiers are
       a table of pointers into .data. */
    uint32_t base;
    int      nsec;
    struct { uint32_t va, vsize, raw, rawsize; } sec[BST_SECTIONS];
} bst_image;

/* Returns a pointer to `need` bytes at a virtual address, or NULL. */
const uint8_t *bst_at(const bst_image *img, uint32_t va, size_t need);

/* Suffixes the normaliser strips, recorded so a later stage can restore the
   sound. */
#define BST_SUF_ED   0x80
#define BST_SUF_ING  0x40
#define BST_SUF_LY   0x20
#define BST_SUF_S    0x10
#define BST_SUF_APOS 0x08

#define BST_WORD_MAX 96

typedef struct {
    char    buf[BST_WORD_MAX];   /* underscore-delimited, e.g. "_hope__" */
    int     len;                 /* characters up to and including the stem */
    int     flags;               /* which suffix came off */
    int     vowels;              /* vowel count, the engine's syllable proxy */
    int     y_from_i;            /* set when -ly stripping rewrote an i as y,
                                    which changes how the suffix is restored */
} bst_word;

int  bst_image_init(bst_image *img, const void *data, size_t len);

/* A phoneme record: a type letter and two operands, which is the form the
   rest of the engine consumes. */
typedef struct {
    uint8_t type;      /* 'T' 'X' 'A' 'S' 'V' 'C' 'P' */
    uint8_t a, b;
} bst_rec;

#define BST_RECS_MAX 64

typedef struct {
    bst_rec rec[BST_RECS_MAX];
    int     n;
} bst_recs;

/* Looks a normalised word up in the dictionary. Returns 1 and fills out on a
   hit, 0 on a miss, in which case the caller should fall back to the rules. */
int  bst_dict_lookup(const bst_image *img, const bst_word *w, bst_recs *out);

/* The rule path produces a phoneme code stream rather than typed records: a
   code that opens a group occupies the middle of three slots, and modifiers
   are written into the slots either side of it. The dictionary's typed records
   are converted into this same form by a later stage. */
#define BST_STREAM_MAX 128

typedef struct {
    uint8_t buf[BST_STREAM_MAX];
    int     len;                 /* bytes written, excluding the leading slot */
} bst_stream;

/* The stream builder both word paths write through. A sound that opens a
   syllable takes four slots, itself and three after it, and a sound that does
   not opens none: it goes into whichever of the open slots its attributes
   select, or is appended. */
typedef struct {
    unsigned char buf[128];
    int pos, before, at, after, stopped;
} bst_builder;

void bst_build_init(bst_builder *b);
void bst_build_emit(const bst_image *img, bst_builder *b, int code);
void bst_build_suffix(const bst_image *img, bst_builder *b, int flags, int y_from_i);
void bst_lts_build(const bst_image *img, const bst_word *w, bst_builder *b);

/* ---- sentence assembly --------------------------------------------------

   Tokens in, one phrase's phoneme stream out. */

typedef struct {
    const bst_image *img;
    uint8_t *s;
    int      wp, hdr, last, len;
    int      emph, punct, hist, mode;
    int      done, full;
    uint8_t  carry[6];
} bst_assembler;

void bst_assemble_init(bst_assembler *z, const bst_image *img, uint8_t *stream);
void bst_assemble_start(bst_assembler *z);
/* Returns non-zero once the phrase is complete. */
int  bst_assemble_token(bst_assembler *z, int kind, uint8_t *buf);

/* Chooses which syllable of a word carries the accent and fills in every
   syllable's mark. `emph` is the emphasis state the surrounding text set and
   `mode` the engine's mode bits. */
void bst_word_stress(const bst_image *img, uint8_t *stream, int len,
                     int emph, int mode);

/* Applies the dictionary's typed records to a code stream, which is the form
   the rest of the engine wants. The two flags carry the "a main stress has
   been placed" and "a secondary has" state across the stem and the suffix, so
   a suffix that wants the stress can take it off the stem.

   Returns the new stream length, or -1 on overflow. */
int  bst_recs_to_stream(const bst_image *img, const bst_rec *rec, int nrec,
                        bst_builder *b, int suffix,
                        int *stress_seen, int *accent_seen);

/* Applies the phonological rule pass to a sentence phoneme stream in place.
   The stream may grow, up to cap: *len is updated and the number of insertions
   returned, because anything the caller holds an offset into shifts by that
   much. */
int  bst_phrules(const bst_image *img, uint8_t *stream, int *len, int cap, int level);

/* Pronounces a word the dictionary does not hold, by the letter-to-sound
   rules. Always succeeds: the rule set has a default for every letter. */
void bst_lts(const bst_image *img, const bst_word *w, bst_stream *out);

/* The whole word path in one call: normalise, look the word up, fall back to
   the rules, and restore the stripped suffix. Returns 1 if the pronunciation
   came from the dictionary, 0 if from the rules.

   Dictionary hits come back as typed records and rule results as a code
   stream, because that is how the engine keeps them; the caller gets whichever
   applies and recs->n is zero when the stream was used. Converting one into
   the other is a later stage that is not reproduced yet. */
int  bst_word_pronounce(const bst_image *img, const char *word,
                        bst_recs *recs, bst_stream *stream);

/* Copies a word into the engine's working form and strips one inflectional
   suffix. Mirrors the original exactly, including that the stem check can
   collapse a doubled consonant, restore a silent e, or refuse the strip. */
void bst_normalise(const bst_image *img, const char *word, bst_word *out);

/* ---- the sentence stream ------------------------------------------------

   Everything past the word front end works on one byte stream per sentence,
   scanned by a handful of cursors that skip empty slots and step over the
   six-byte command records. The stages share the cursors, so they live here
   rather than inside any one of them. */

typedef struct { int val, pos; } bst_cur;

/* The two phoneme attribute bytes. The first classifies the sound; the second
   says which of the scans below stop on it. */
int bst_ph_attr1(const bst_image *img, int c);
int bst_ph_attr2(const bst_image *img, int c);

/* Forward to the next segment, stress mark or eighth-class sound; backward to
   the previous eighth-class sound; and forward to the next segment that opens
   a group. Each gives up slightly differently at the end of the stream, which
   is load-bearing: a cursor that reads zero because its scan found nothing is
   distinguishable from one that found a zero. */
void bst_scan_seg(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_stress(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_eight(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_eight_back(const bst_image *img, const uint8_t *s, int from, bst_cur *c);
void bst_scan_strong(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);

/* ---- the pair scan ------------------------------------------------------

   One traversal of the sentence stream emitting, for each sound, a diphone
   segment naming it and the sound before it, and up to three transition
   events around it carrying duration and two pitch offsets. How many events
   fall before the segment and how many after is fixed per sound class, and
   the count of events since the last segment is what the segment records, so
   the two kinds interleave and cannot be produced separately. */

#define BST_EMIT_SEG   0
#define BST_EMIT_TRANS 1

typedef struct {
    uint8_t  kind;
    uint16_t index;   /* segment: previous * 48 + current, or a command with
                         0xF000 set; transition: (sound - 1) * 3 + position */
    uint16_t count;   /* segment: events since the last one; transition: the
                         duration, before the engine clamps it to a byte */
    int16_t  a, b;    /* segment: the command's operands; transition: the two
                         pitch offsets, 0x7F meaning "no target" */
} bst_emit;

/* State the scan carries between sentences. */
typedef struct {
    int prev;        /* the sound the next sentence's first pair starts from */
    int strong;      /* the group-opening cursor's value, kept across calls */
    int emphasis;
    int flags;
} bst_pair_state;

/* Returns the number of records written, or -1 if max was too small. */
int bst_pairs(const bst_image *img, const uint8_t *stream, int len,
              bst_pair_state *st, bst_emit *out, int max);

/* ---- accent assignment --------------------------------------------------

   Fills the empty slots after each group opener with pitch-target codes, which
   is the input the contour generator reads. Returns the number of accents the
   sentence has. */

typedef struct {
    int flags;      /* the stream's own mode bits */
    int carried;    /* the level the previous phrase ended on, in and out */
    int level;      /* the voice's pitch level index, in and out */
    int tail;       /* offset of the slot that hands a level on, or -1 */
    int emphasis;   /* set on return if an emphasis command was in force */
} bst_accent_state;

int bst_accents(const bst_image *img, uint8_t *stream, int len,
                bst_accent_state *st);

/* ---- the intonation contour ---------------------------------------------

   The voice's pitch table spans its range in fourteen steps; an accent code is
   an index into it. The contour generator walks the accents emitting one
   record each: the pitch period to head for, how many segments to take getting
   there, and the shape of the move. */

typedef struct {
    int   base;        /* the voice's floor, in hertz */
    int   top;         /* the stored ceiling the range commands scale */
    int   voicebase;   /* the floor to fall back to */
    int   level;       /* the default pitch level index */
    int   emphasis;
    int   voice;
    int   strong;      /* the group cursor's value, carried between calls */
    int   mid, hi;     /* the current middle and ceiling */
    short table[14];
} bst_voice;

typedef struct {
    uint8_t kind;      /* 2 a move, 1 the close */
    uint8_t period;
    int16_t dur;
    int16_t slope;
} bst_contour_rec;

int bst_contour(const bst_image *img, const uint8_t *stream, int len,
                bst_voice *v, bst_contour_rec *out, int max);

/* ---- diphone expansion --------------------------------------------------

   A segment's index names an entry in the diphone inventory, and the entry
   holds one sub-sequence of acoustic targets per position the sound covers.
   The segment's count says how many positions to take. */

int  bst_diphone_count(const bst_image *img);
int  bst_diphone(const bst_image *img, int index, int positions,
                 uint16_t *out, int max);
void bst_target_coeffs(const bst_image *img, int voice, int target, int16_t k[10]);

#endif
