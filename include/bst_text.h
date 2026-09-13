#ifndef BST_TEXT_H
#define BST_TEXT_H

#include <stdint.h>
#include <stddef.h>

/* The word front end: normalisation, dictionary lookup and letter-to-sound.
   Everything here reads its tables out of a loaded BeSTspeech image rather
   than embedding them, so the library carries no lifted data of its own. */

typedef struct {
    const uint8_t *image;
    size_t         len;
} bst_image;

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

/* Applies the phonological rule pass to a sentence phoneme stream in place.
   Returns the number of rewrites; the stream may grow, up to cap. */
int  bst_phrules(const bst_image *img, uint8_t *stream, int len, int cap, int emphasis);

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

#endif
