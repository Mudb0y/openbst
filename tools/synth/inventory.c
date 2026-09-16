#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"
#include "bst_text.h"
#include "bst_priv.h"
#include "corpus.h"

/* What the shipped tables are made of.
 *
 * src/data holds whole data sections of the original modules, because the
 * lift keeps the section a read landed in rather than only the bytes it
 * touched. So what we ship is a snapshot of an address space. It is correct
 * and it is opaque: nothing in it says which bytes are the dictionary, which
 * are the rules and which are the synthesis parameters, and none of it can
 * be edited without moving everything that points past it.
 *
 * This measures the snapshot. Every address the build directory names becomes
 * an anchor; the corpus is then driven through the build and each byte it
 * reads is attributed to the nearest anchor at or below it. That gives each
 * table an extent that was measured rather than guessed, and it gives the
 * share of the shipped bytes each family of tables accounts for.
 *
 * Two things it deliberately cannot see. Bytes no anchor precedes are
 * reported as unattributed rather than folded into a neighbour, because a
 * table reached only through a pointer in another table has no anchor and
 * would otherwise inflate whatever sits before it. And bytes the corpus never
 * reads are reported as unread rather than dropped: most of them are the
 * tail of a dictionary, which is exactly the part a corpus cannot reach and
 * exactly the part that has to survive into any format we invent. */

#define NONE ((size_t)-1)
#define MAXANCH 512

typedef struct {
    const char *family;
    const char *name;
    uint32_t    va;
    size_t      off;    /* where it lands in the shipped image */
    size_t      last;   /* the highest offset the corpus read through it */
    size_t      read;   /* how many bytes it accounts for */
} anchor;

static anchor anch[MAXANCH];
static int    nanch;

static unsigned char *touched;
static size_t         touched_len;

static void watch(const bst_image *img, size_t off, size_t need) {
    (void)img;
    for (size_t i = 0; i < need && off + i < touched_len; i++)
        touched[off + i] = 1;
}

/* Resolve through the library's own reader, so an address means here exactly
   what it means to the engine, including a 16-bit module's segment pair. */
static size_t off_of(const bst_image *img, uint32_t va) {
    const uint8_t *p = bst_at(img, va, 1);
    return p ? (size_t)(p - img->image) : NONE;
}

static void add(const bst_image *img, const char *family,
                const char *name, uint32_t va) {
    if (!va || nanch >= MAXANCH) return;
    size_t off = off_of(img, va);
    if (off == NONE) return;
    anch[nanch].family = family;
    anch[nanch].name   = name;
    anch[nanch].va     = va;
    anch[nanch].off    = off;
    anch[nanch].last   = NONE;
    anch[nanch].read   = 0;
    nanch++;
}

/* The directory's addresses, grouped the way the pipeline uses them. The
   token handlers in h[] and the code bounds are left out: they point into the
   module's code, which is not shipped and never read. */
static void collect(const bst_image *img) {
    const bst_tabmap *m = &img->t;
    nanch = 0;

#define A(fam, field) add(img, fam, #field, m->field)
    A("chars", chattr);      A("chars", letterattr);
    A("chars", casemap);     A("chars", symmap);
    A("chars", xlat_first);  A("chars", xlat_second);

    A("phonemes", phattr1);  A("phonemes", phattr2);
    A("phonemes", classtab); A("phonemes", exctab);
    A("phonemes", basedur);  A("phonemes", coefgain);

    A("tokeniser", tokstates); A("tokeniser", names);

    A("words", prefixes); A("words", split_tab); A("words", suffix_ptrs);

    A("dictionary", code_medial);  A("dictionary", code_initial);
    A("dictionary", ph_single);    A("dictionary", ph_single_max);
    A("dictionary", ph_pair);      A("dictionary", ph_pair_max);

    A("lts", lts_index); A("lts", dispatch); A("lts", rules);
    A("lts", patterns);  A("lts", outputs);

    A("trie", trie_desc);

    A("modifiers", modmap); A("modifiers", modtab); A("modifiers", modtab_rows);

    A("prosody", trans_pitch); A("prosody", vowel_dur);
    A("prosody", stress_num);  A("prosody", stress_add);
    A("prosody", sound_add);

    A("diphones", diph_records); A("diphones", diph_offsets);
    A("diphones", diph_offsets_end); A("diphones", voices);
#undef A

    for (int i = 0; i < BST_BUCKETS; i++) {
        add(img, "dictionary", "bucket_index", m->bucket_index[i]);
        add(img, "dictionary", "bucket_data",  m->bucket_data[i]);
    }
    for (int i = 0; i < BST_S_COUNT; i++)
        add(img, "numbers", "number_word", m->s[i]);
}

static int by_off(const void *a, const void *b) {
    size_t x = ((const anchor *)a)->off, y = ((const anchor *)b)->off;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* The anchor a byte belongs to: the last one at or below it. */
static int owner(size_t off) {
    int lo = 0, hi = nanch - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (anch[mid].off <= off) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

typedef struct { const char *name; size_t tables, span, read; } famsum;
static famsum fam[32];
static int nfam;

static famsum *famof(const char *name) {
    for (int i = 0; i < nfam; i++)
        if (strcmp(fam[i].name, name) == 0) return &fam[i];
    fam[nfam].name = name;
    fam[nfam].tables = fam[nfam].span = fam[nfam].read = 0;
    return &fam[nfam++];
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "--tables") == 0;

    const char *names[64];
    int nb = bst_builds(names, 64);
    if (nb > 64) nb = 64;

    size_t all_shipped = 0, all_read = 0, all_unattr = 0;

    for (int b = 0; b < nb; b++) {
        bst *h = bst_open(names[b]);
        if (!h) { fprintf(stderr, "inventory: cannot open %s\n", names[b]); return 1; }
        const bst_image *img = bst_handle_image(h);

        touched_len = img->len;
        touched = calloc(touched_len ? touched_len : 1, 1);
        if (!touched) return 1;

        collect(img);
        qsort(anch, (size_t)nanch, sizeof anch[0], by_off);

        bst_read_hook = watch;
        bst_corpus_sweep(h);
        bst_read_hook = NULL;

        /* What is shipped is the chunks, not the span: the image is a span
           with holes in it where the lift dropped a section, and a read that
           lands in a hole finds the zeros the original would have had past
           the end of a table. Those reads are counted separately, because
           they are reads of nothing and not of a table. */
        const bst_lifted *d = bst_lifted_for(names[b]);
        if (!d) { fprintf(stderr, "inventory: no data for %s\n", names[b]); return 1; }
        unsigned char *inchunk = calloc(touched_len ? touched_len : 1, 1);
        if (!inchunk) return 1;
        size_t shipped = 0;
        for (int i = 0; i < d->nchunk; i++) {
            size_t a = d->chunk[i].off, n = d->chunk[i].len;
            if (a + n > touched_len) n = touched_len > a ? touched_len - a : 0;
            memset(inchunk + a, 1, n);
            shipped += n;
        }

        size_t read = 0, unattr = 0, hole = 0;
        for (size_t i = 0; i < touched_len; i++) {
            if (!touched[i]) continue;
            if (!inchunk[i]) { hole++; continue; }
            read++;
            int k = owner(i);
            if (k < 0) { unattr++; continue; }
            anch[k].read++;
            if (anch[k].last == NONE || i > anch[k].last) anch[k].last = i;
        }
        free(inchunk);

        nfam = 0;
        for (int i = 0; i < nanch; i++) {
            famsum *f = famof(anch[i].family);
            f->tables++;
            f->read += anch[i].read;
            if (anch[i].last != NONE) f->span += anch[i].last - anch[i].off + 1;
        }

        printf("build %s shipped %zu read %zu unread %zu unattributed %zu "
               "past-the-end %zu\n",
               names[b], shipped, read, shipped > read ? shipped - read : 0,
               unattr, hole);
        for (int i = 0; i < nfam; i++)
            printf("  %-11s %3zu tables span %7zu read %7zu\n",
                   fam[i].name, fam[i].tables, fam[i].span, fam[i].read);

        if (verbose)
            for (int i = 0; i < nanch; i++)
                printf("    %-11s %-17s va %08X off %6zu extent %6zu read %6zu\n",
                       anch[i].family, anch[i].name, anch[i].va, anch[i].off,
                       anch[i].last == NONE ? 0 : anch[i].last - anch[i].off + 1,
                       anch[i].read);

        all_shipped += shipped;
        all_read += read;
        all_unattr += unattr;

        free(touched);
        bst_close(h);
    }

    printf("inventory: %d builds, %zu bytes shipped, %zu read, %zu unattributed\n",
           nb, all_shipped, all_read, all_unattr);
    return 0;
}
