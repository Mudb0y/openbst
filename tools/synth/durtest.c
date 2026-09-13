#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_synth.h"

/* Checks the duration model against triples of (record byte, rate byte,
   duration) captured from the engine at the instruction that computes it. */

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: durtest DLL TRIPLES\n"); return 2; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *img = malloc(n);
    bst_tables t;
    int rc = (img && fread(img, 1, n, f) == (size_t)n) ? bst_tables_load(&t, img, n) : -1;
    free(img);
    fclose(f);
    if (rc < 0) { fprintf(stderr, "table load failed\n"); return 1; }

    /* The engine was running at its default rate, so the scale table is the
       stored base table unchanged. */
    int16_t scale[16];
    bst_duration_scale(t.duration, 0, scale);

    FILE *g = fopen(argv[2], "r");
    if (!g) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

    unsigned rec, rate;
    int dur, pass = 0, fail = 0;
    while (fscanf(g, "%x %x %d", &rate, &rec, &dur) == 3) {
        int ours = bst_segment_duration(scale, (int)rec, (int)rate, 0);
        if (ours == dur) pass++;
        else {
            if (!fail)
                fprintf(stderr, "first mismatch: record %02X rate %02X -> ours %d theirs %d\n",
                        rec, rate, ours, dur);
            fail++;
        }
    }
    fclose(g);
    printf("duration: %d exact, %d differing\n", pass, fail);
    return fail ? 1 : 0;
}
