#ifndef BST_TEXT_H
#define BST_TEXT_H

#include <stdint.h>
#include <stddef.h>

/* The word front end: normalisation, dictionary lookup and letter-to-sound.
   Everything here reads its tables out of a loaded BeSTspeech image rather
   than embedding them, so the library carries no lifted data of its own. */

#define BST_SECTIONS 20

/* ---- the table directory ------------------------------------------------

   Every address the library reads out of an image is named here rather than
   written into the code, because the same engine ships in three generations
   and nineteen languages and only the addresses move. A build is described by
   one bst_tabmap; the code never mentions a number.

   Addresses are whatever the image's own addressing is: a virtual address in
   a PE build, a segment and offset packed as (segment << 16) | offset in a
   16-bit NE one. bst_at resolves either. */

enum {
    BST_H_LETTER, BST_H_DIGIT, BST_H_EAT1, BST_H_SPACE, BST_H_DOT,
    BST_H_CURRENCY, BST_H_PUNCT, BST_H_DASH, BST_H_EXPONENT, BST_H_MODE1,
    BST_H_MODE2, BST_H_DEL, BST_H_OPENER, BST_H_TILDE, BST_H_APOSDOT,
    BST_H_APOS, BST_H_POSSESS, BST_H_EAT2, BST_H_EAT3, BST_H_WORD,
    BST_H_NUMBER, BST_H_DOTTED, BST_H_SEP, BST_H_GROUPS, BST_H_SEPNUM,
    BST_H_MONEY, BST_H_DASH2, BST_H_ORDINAL, BST_H_ORDEMIT, BST_H_PUNCTOUT,
    BST_H_DOTOUT,
    BST_H_COUNT
};

enum {
    BST_S_POINT, BST_S_DOLLARS, BST_S_AND, BST_S_CENTS,
    BST_S_ORD_ST, BST_S_ORD_ND, BST_S_ORD_RD, BST_S_ORD_TIETH,
    BST_S_ORD_FIFTH, BST_S_ORD_FIRST, BST_S_ORD_TH, BST_S_GRPSEP,
    BST_S_PLURAL, BST_S_DIGITS, BST_S_TENS, BST_S_TEENS, BST_S_SCALES,
    BST_S_OH, BST_S_HUNDRED, BST_S_ZERO,
    BST_S_COUNT
};

#define BST_BUCKETS 15

typedef struct {
    /* character classification */
    uint32_t chattr, letterattr, casemap, symmap;
    /* phoneme attributes and the duration tables the frame builder reads */
    uint32_t phattr1, phattr2, classtab, exctab, basedur, coefgain;
    /* the tokeniser. code_lo and code_hi bound the executable section, which
       is how a transition row is told from the bytes after the last one: a row
       names its handler by address. The row itself is three columns whose
       widths and places differ between the 32-bit builds and the 16-bit one,
       which packs the two state columns into bytes and the handler into a near
       pointer, with a link to the next row between them. */
    uint32_t tokstates, names, code_lo, code_hi;
    uint8_t  tok_stride;
    uint8_t  tok_state_off, tok_state_w;
    uint8_t  tok_handler_off, tok_handler_w;
    uint8_t  tok_next_off, tok_next_w;

    /* The other records whose packing differs between generations. The 16-bit
       build squeezes a rule's priority into a byte and drops a byte of padding
       from a letter-to-sound index entry; its trie descriptor holds far
       pointers where the 32-bit one holds flat ones, which read the same way
       but sit at different offsets. */
    uint8_t  lts_index_stride;
    uint8_t  rule_stride, rule_prio_w;
    uint8_t  trie_st_off, trie_li_off, trie_base_off, trie_max_off, trie_po_off;

    /* Three constants the frame builder holds rather than reads. A silent
       frame's first byte, and how an unvoiced frame is chunked: the 1995 build
       holds it for one period of a hundred and ten samples, the 1998 one for
       eight periods of an eighth of that. The frame lasts as long either way,
       so the interpolation clock does not change; only the bytes do. */
    uint8_t  silence_f0, unvoiced_dur, unvoiced_reps;

    /* The shortest period a frame may carry, which is also the period a
       silent frame is given. Sixty-six everywhere but the 2006 builds. */
    uint8_t  min_period;

    /* Which coefficients round the interpolation's log index up rather than
       down, so a step over an odd distance is one larger. The 2006 builds do
       it for the first three; the earlier ones for none. */
    uint16_t interp_round_mask;

    /* Which coefficients the frame rounds away from zero on its way from the
       smoothed state to the byte a frame carries. The 2006 builds do it for
       the second and third; every other build truncates all ten. */
    uint16_t coef_round_mask;

    /* Where a segment record keeps its excitation class. The 1995 and 1998
       builds put the target index in the low nine bits and the class above
       it; the 2006 builds leave four bits between them and shift by thirteen.
       The first byte of a long silent stretch, and whether the excitation
       nibble has the two extra cases the earlier builds carry, go the same
       way: they are the shape of a frame, not a table. */
    uint8_t  class_shift;
    uint8_t  long_silence_f0;
    uint8_t  exc_two_way;

    /* The 2006 builds compose an unvoiced frame as one long period and split
       it as it leaves: the repeat count goes in the first byte's low nibble,
       the period is divided by it, and the fractional part the second byte
       carries is dropped. */
    uint8_t  unvoiced_chunk;

    /* The 1995 and 1998 builds put a break into a sentence longer than one
       breath, at the last word whose opening marker allows one. The 2006
       builds dropped the rule and say a sentence in one phrase however long
       it is. */
    uint8_t  no_breath_break;

    /* The text has a closing brace appended to it, which shuts the last
       phrase and opens one more holding nothing but a pause. The earlier
       builds speak that phrase; the 2006 ones stop instead, and what reaches
       the buffer in its place is the twenty silent samples of their own
       shutdown. */
    uint8_t  no_closing_phrase;

    /* What a transition's stored duration is multiplied by on its way to a
       sample count. Fifty-six in the earlier builds, fifty-three in the 2006
       ones. */
    uint8_t  dur_mult;

    /* The 2006 builds add half a step before every shift in the gain and
       pitch smoothers, so those round to nearest where the earlier builds
       truncate. */
    uint8_t  nearest_round;

    /* The numerator a pitch frequency is divided into to get a period in
       samples: the build's nominal sample rate. Eleven thousand and
       twenty-five in the earlier builds, ten thousand four hundred in the
       2006 ones, which is also why their transition multiplier is smaller. */
    uint16_t pitch_rate;

    /* The two constants that turn an intonation record's slope and duration
       into a sample count. They scale with the sample rate: 177 and 940 in
       the earlier builds, 167 and 887 in the 2006 ones. */
    uint16_t slope_mult, inton_dur_mult;

    /* A record with no duration of its own spreads its slope over the time
       left. The earlier builds divide by eighty-five; the 2006 ones multiply
       by three and shift down eight, which is eighty-five and a third. */
    uint8_t  inton_slope_shift;

    /* The 1998 build halves a vowel's duration on the way out of the pair
       scan, after the floors rather than before them, and rounds the
       transition's own halving down where the 1995 build rounds it up. */
    uint8_t  vowel_dur_shift, trn_round;

    /* How far this language's marks sit above English's. The six 1998 modules
       are one body of code compiled once each with a different number of
       sounds, so a sound code means the same thing in all of them and every
       code above the inventory -- the stress marks, the phrase markers, the
       command byte -- is shifted by the difference. The library keeps its
       streams in English numbering and shifts only where it indexes one of the
       engine's own tables by a code. */
    int8_t   code_shift;

    /* Targets per voice. The diphone segment holds every voice's targets
       before the records, so this says where the records start as well as how
       to reach a voice above the first. */
    uint16_t voice_span;
    /* Bytes per stored coefficient. The 2006 builds widen each to four and
       read the low byte, and they carry one voice, so their span is zero. */
    uint8_t  voice_stride;
    /* The 2006 build's duration scaling is the same arithmetic reached by
       shifts: a byte table over a power of two where the earlier builds hold
       a numerator and a denominator, and the divisions around it open-coded
       as arithmetic shifts, which floor where a divide truncates. Zero means
       the pair table and the divides. */
    uint8_t  stress_shift;

    /* the dictionary. A build that holds the two counts as immediates rather
       than in memory leaves the address zero and gives the count instead. */
    uint32_t code_medial, code_initial;
    uint32_t ph_single, ph_single_max, ph_pair, ph_pair_max;
    int16_t  ph_single_n, ph_pair_n;
    uint32_t bucket_index[BST_BUCKETS], bucket_data[BST_BUCKETS];
    /* letter to sound */
    uint32_t suffix_ptrs, lts_index, dispatch, rules, patterns, outputs;
    /* the exception trie */
    uint32_t trie_desc;
    /* the record-to-stream conversion */
    uint32_t modmap, modtab;
    /* the pair scan */
    uint32_t trans_pitch, vowel_dur, stress_num, stress_add, sound_add;
    /* diphones and voices */
    uint32_t diph_records, diph_offsets, diph_offsets_end, voices;

    uint32_t h[BST_H_COUNT];
    uint32_t s[BST_S_COUNT];
} bst_tabmap;

extern const bst_tabmap BST_MAP_1995;
extern const bst_tabmap BST_MAP_1998_ENG;
extern const bst_tabmap BST_MAP_1998_DUT;
extern const bst_tabmap BST_MAP_1998_FRN;
extern const bst_tabmap BST_MAP_1998_GRM;
extern const bst_tabmap BST_MAP_1998_ITL;
extern const bst_tabmap BST_MAP_1998_SPN;
extern const bst_tabmap BST_MAP_2006_ENG;

/* Fills names with the entries the map has not been given and returns how
   many there were, so an incomplete build can say so rather than misbehave. */
int bst_map_gaps(const bst_tabmap *m, const char **names, int max);


typedef struct {
    const uint8_t *image;
    size_t         len;
    /* The section map, so an address in the image can be followed wherever it
       points. Most tables live in .rdata, but the pronunciation modifiers are
       a table of pointers into .data. */
    uint32_t base;
    int      nsec;
    struct { uint32_t va, vsize, raw, rawsize; } sec[BST_SECTIONS];
    bst_tabmap t;
    /* Set when the image is one the library built rather than one the caller
       handed over: a 16-bit module has to be laid out and relocated before
       any of its pointers mean anything. */
    void *own;
} bst_image;

/* Set to print each stage's working as it runs -- the stream as assembled,
   after the rule pass and at the pair scan, the transition records, the vowel
   durations and the gain smoother's inputs. What a build is compared against
   the engine on when its frames stop agreeing. */
extern int bst_trace;

/* Returns a pointer to `need` bytes at a virtual address, or NULL. */
const uint8_t *bst_at(const bst_image *img, uint32_t va, size_t need);

/* The three accessors every table read goes through. An address outside the
   image reads as zero, which is what the original does when a table index runs
   off the end of its table into whatever follows. */
static inline int bst_u8(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i, 1);
    return p ? p[0] : 0;
}
static inline int bst_u16(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i * 2, 2);
    return p ? (p[0] | (p[1] << 8)) : 0;
}
static inline int bst_s16(const bst_image *img, uint32_t va, int i) {
    return (int16_t)bst_u16(img, va, i);
}
static inline uint32_t bst_u32(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i * 4, 4);
    return p ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)) : 0;
}

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
/* The same, with the table directory given rather than assumed. */
int  bst_image_init_map(bst_image *img, const void *data, size_t len,
                        const bst_tabmap *map);

/* A 16-bit NE module. Its segments are laid out one to a 64K window and its
   relocations applied, because a pointer in the file holds a link in a fixup
   chain rather than an address. Addresses are then (segment << 16) | offset,
   which is what an applied internal relocation writes, so a pointer read out
   of the loaded image is already in the form bst_at wants.

   Call bst_image_free when done; the other two initialisers need no freeing
   but tolerate it. */
int  bst_image_init_ne(bst_image *img, const void *data, size_t len,
                       const bst_tabmap *map);
void bst_image_free(bst_image *img);

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

/* A stream code as this build's tables index it. Sounds are numbered the same
   everywhere; marks are not. */
static inline int bst_code(const bst_image *img, int c) {
    /* English's inventory is forty-eight sounds, 0x00 to 0x2F, and the last
       of them is the pause. A language with more sounds puts its pause and
       every mark above it further up, so the boundary is the pause itself. */
    return c >= 0x2F ? (c + img->t.code_shift) & 0xFF : c;
}

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
