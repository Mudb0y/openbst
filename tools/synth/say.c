#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the word path over each word given and prints what it produces, so the
   library can be exercised without the emulator. */

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: say DLL WORD...\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    bst_image img;
    if (bst_image_init(&img, d, n) < 0) { fprintf(stderr, "bad image\n"); return 1; }

    for (int i = 2; i < argc; i++) {
        bst_recs r;
        bst_stream s;
        bst_word w;
        bst_normalise(&img, argv[i], &w);
        int hit = bst_word_pronounce(&img, argv[i], &r, &s);
        printf("%-14s %-14s ", argv[i], w.buf);
        if (hit) {
            printf("dictionary:");
            for (int k = 0; k < r.n; k++)
                printf(" %c%d,%d", r.rec[k].type, r.rec[k].a, r.rec[k].b);
        } else {
            printf("rules:     ");
            for (int k = 0; k < s.len; k++) printf(" %02X", s.buf[k]);
        }
        putchar('\n');
    }
    free(d);
    return 0;
}
