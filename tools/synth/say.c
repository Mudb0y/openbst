#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the word path over each word given and prints what it produces, so the
   library can be exercised without the emulator. */

extern const bst_tabmap *bst_map_named(const char *name);

int main(int argc, char **argv) {
    const char *mapname = NULL;
    if (argc > 2 && !strcmp(argv[1], "--map")) { mapname = argv[2]; argv += 2; argc -= 2; }
    if (argc > 1 && !strcmp(argv[1], "--trace")) { bst_trace = 1; argv++; argc--; }
    if (argc < 3) { fprintf(stderr, "usage: say [--map NAME] DLL WORD...\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    bst_image img;
    const bst_tabmap *map = mapname ? bst_map_named(mapname) : NULL;
    if (mapname && !map) { fprintf(stderr, "no map named %s\n", mapname); return 2; }
    int rc = map ? bst_image_init_map(&img, d, n, map) : bst_image_init(&img, d, n);
    if (rc < 0) { fprintf(stderr, "bad image\n"); return 1; }

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
