#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_synth.h"

/* Replays the before/after snapshots "oracle --gain" and "oracle --pitch"
   capture around the engine's own smoothing steps. */

static int load_tables(bst_tables *t, const char *dll) {
    FILE *f = fopen(dll, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *img = malloc(n);
    int rc = -1;
    if (img && fread(img, 1, n, f) == (size_t)n) rc = bst_tables_load(t, img, n);
    free(img);
    fclose(f);
    return rc;
}

/* Reads the n'th pipe-separated field of a capture line as a little-endian
   16-bit value, or a single byte when width is 1. */
static int field(const char *line, int idx, int width, unsigned *out) {
    const char *p = line;
    for (int i = 0; i <= idx; i++) {
        p = strchr(p, '|');
        if (!p) return -1;
        p++;
    }
    unsigned b[2] = { 0, 0 };
    if (sscanf(p, " %2x %2x", &b[0], &b[1]) < width) return -1;
    *out = width == 1 ? b[0] : (b[0] | (b[1] << 8));
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: prosodytest DLL CAPTURE gain|pitch\n"); return 2; }
    int is_gain = strcmp(argv[3], "gain") == 0;

    bst_tables t;
    if (load_tables(&t, argv[1]) < 0) { fprintf(stderr, "table load failed\n"); return 1; }
    FILE *f = fopen(argv[2], "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

    char line[4096];
    unsigned st = 0, tgt = 0, clock = 0, dur = 0, cls = 0, fresh = 0, base = 0, adj = 0;
    int have = 0, pass = 0, fail = 0, passv = 0;

    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'B') {
            if (field(line, 0, 2, &st) || field(line, 1, 2, &tgt) ||
                field(line, 2, 2, &clock) || field(line, 3, 2, &dur)) continue;
            if (is_gain) {
                if (field(line, 4, 2, &cls) || field(line, 5, 1, &fresh) ||
                    field(line, 6, 2, &base) || field(line, 7, 2, &adj)) continue;
            }
            have = 1;
        } else if (line[0] == 'A' && have) {
            unsigned after = 0, second = 0;
            if (field(line, 0, 2, &after)) { have = 0; continue; }
            field(line, 1, is_gain ? 1 : 2, &second);

            unsigned ours, oursv;
            if (is_gain) {
                bst_gain g;
                bst_gain_init(&g, &t);
                g.acc = (int16_t)st;
                oursv = (unsigned)bst_gain_value(&g, (int16_t)base, (int16_t)adj, (int)cls);
                if (!fresh) bst_gain_step(&g, (int16_t)tgt, (int16_t)dur, (int16_t)clock);
                ours = (unsigned)(uint16_t)g.acc;
            } else {
                bst_pitch p;
                bst_pitch_init(&p, &t);
                p.acc = (uint16_t)st;
                bst_pitch_step(&p, (uint16_t)tgt, (int)dur, (int)clock);
                ours = p.acc;
                oursv = (unsigned)bst_pitch_period(&p);
            }

            if (ours == after) pass++;
            else {
                if (!fail)
                    fprintf(stderr, "first mismatch: state %u target %u dur %u clock %u -> ours %u theirs %u\n",
                            st, tgt, dur, clock, ours, after);
                fail++;
            }
            if (oursv == second) passv++;
            have = 0;
        }
    }
    fclose(f);
    printf("%s: %d exact, %d differing (derived value matched %d)\n",
           argv[3], pass, fail, passv);
    return fail ? 1 : 0;
}
