#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"
#include "bst_synth.h"
#include "bst_text.h"
#include "bst_priv.h"
#include "corpus.h"

/* Takes the tables a build needs out of its binary and writes them as C, so
 * that the library speaks with nothing else present.
 *
 * Which bytes it takes is decided by watching every read the engine makes
 * while it says a wide corpus, and then keeping whole the sections those
 * reads landed in. Keeping whole sections rather than only the bytes seen is
 * deliberate: a dictionary record is read only for a word that is in the
 * dictionary, so coverage would quietly drop most of it and the words it
 * covers would fall back on the rules. The sections the engine never reads
 * at all, which is its own code, are what this leaves behind. */

static unsigned char *touched;
static size_t         touched_len;

static void watch(const bst_image *img, size_t off, size_t need) {
    (void)img;
    for (size_t i = 0; i < need && off + i < touched_len; i++)
        touched[off + i] = 1;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n + 1);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
    fclose(f);
    d[n] = 0;
    if (len) *len = (size_t)n;
    return d;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: lift BUILD DLL OUTDIR [--core CORE] [WORDLIST...]\n");
        return 2;
    }
    const char *name = argv[1], *dllpath = argv[2], *outdir = argv[3];
    const char *corepath = NULL;
    int first = 4;
    if (argc > 5 && strcmp(argv[4], "--core") == 0) { corepath = argv[5]; first = 6; }

    size_t len, corelen = 0;
    char *image = slurp(dllpath, &len);
    if (!image) { fprintf(stderr, "lift: cannot read %s\n", dllpath); return 1; }
    char *core = NULL;
    if (corepath && !(core = slurp(corepath, &corelen))) {
        fprintf(stderr, "lift: cannot read %s\n", corepath);
        return 1;
    }

    bst *h = bst_open_images(name, image, len, core, corelen);
    if (!h) { fprintf(stderr, "lift: cannot open %s as %s\n", dllpath, name); return 1; }

    /* The laid-out image, which for a 16-bit module is not the file. */
    const bst_image *img = bst_handle_image(h);
    touched_len = img->len;
    touched = calloc(touched_len ? touched_len : 1, 1);
    if (!touched) return 1;

    bst_read_hook = watch;
    bst_corpus_sweep(h);
    for (int i = first; i < argc; i++) bst_corpus_file(h, argv[i]);
    bst_read_hook = NULL;

    /* Keep whole every section a read landed in. */
    int keep[BST_SECTIONS];
    memset(keep, 0, sizeof keep);
    size_t total = 0, seen = 0;
    for (int s = 0; s < img->nsec; s++) {
        size_t a = img->sec[s].raw;
        size_t n = img->sec[s].rawsize;
        if (img->sec[s].vsize > n) n = img->sec[s].vsize;
        if (a + n > img->len) n = img->len > a ? img->len - a : 0;
        size_t hit = 0;
        for (size_t i = 0; i < n; i++) if (touched[a + i]) hit++;
        if (hit) { keep[s] = 1; total += n; seen += hit; }
        fprintf(stderr, "  sec %d va %06X raw %06X size %6zu read %6zu%s\n",
                s, img->sec[s].va, img->sec[s].raw, n, hit, hit ? "" : "  dropped");
    }

    char path[512];
    snprintf(path, sizeof path, "%s/%s.c", outdir, name);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "lift: cannot write %s\n", path); return 1; }

    fprintf(f, "/* Written by tools/lift out of %s. These are Berkeley Speech\n"
               "   Technologies' tables, not ours; see LICENSE. */\n\n"
               "#include \"bst_text.h\"\n\n", dllpath);

    for (int s = 0; s < img->nsec; s++) {
        if (!keep[s]) continue;
        size_t a = img->sec[s].raw;
        size_t n = img->sec[s].rawsize;
        if (img->sec[s].vsize > n) n = img->sec[s].vsize;
        if (a + n > img->len) n = img->len > a ? img->len - a : 0;
        fprintf(f, "static const uint8_t s%d[%zu] = {", s, n);
        for (size_t i = 0; i < n; i++) {
            if (i % 16 == 0) fprintf(f, "\n");
            fprintf(f, "%d,", img->image[a + i]);
        }
        fprintf(f, "\n};\n\n");
    }

    fprintf(f, "static const bst_section sec[] = {\n");
    for (int s = 0; s < img->nsec; s++)
        fprintf(f, "    { 0x%X, 0x%X, 0x%X, 0x%X },\n", img->sec[s].va,
                img->sec[s].vsize, img->sec[s].raw, img->sec[s].rawsize);
    fprintf(f, "};\n\n");

    fprintf(f, "static const bst_chunk chunk[] = {\n");
    for (int s = 0; s < img->nsec; s++) {
        if (!keep[s]) continue;
        fprintf(f, "    { 0x%X, sizeof s%d, s%d },\n", img->sec[s].raw, s, s);
    }
    fprintf(f, "};\n\n");

    bst_tables tab;
    bst_handle_tables(h, &tab);
    const uint8_t *tb = (const uint8_t *)&tab;
    fprintf(f, "static const uint8_t lat[%zu] = {", sizeof tab);
    for (size_t i = 0; i < sizeof tab; i++) {
        if (i % 16 == 0) fprintf(f, "\n");
        fprintf(f, "%d,", tb[i]);
    }
    fprintf(f, "\n};\n\n");

    /* The buffer only has to reach the last section kept: nothing past it is
       ever read, and a read that ran off the end of the original found the
       zeros this leaves behind. */
    size_t size = 0;
    for (int s = 0; s < img->nsec; s++) {
        if (!keep[s]) continue;
        size_t a = img->sec[s].raw;
        size_t n = img->sec[s].rawsize;
        if (img->sec[s].vsize > n) n = img->sec[s].vsize;
        if (a + n > img->len) n = img->len > a ? img->len - a : 0;
        if (a + n > size) size = a + n;
    }
    fprintf(f, "const bst_lifted BST_DATA_%s = {\n"
               "    0x%X, 0x%zX, %d, sec,\n"
               "    (int)(sizeof chunk / sizeof chunk[0]), chunk, lat\n"
               "};\n", name, img->base, size, img->nsec);
    fclose(f);

    fprintf(stderr, "lift %-8s %zu of %zu bytes read, %zu kept\n",
            name, seen, img->len, total);
    bst_close(h);
    free(touched);
    free(image);
    return 0;
}
