#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_synth.h"

/* Replays the before/after snapshots that "oracle --interp" captures around
   the engine's own interpolation step and requires our step to land on the
   same state. */

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

static int hexbytes(const char *s, uint8_t *out, int max) {
    int n = 0;
    while (n < max) {
        while (*s == ' ') s++;
        if (*s == '|' || *s == '\0' || *s == '\n') break;
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void words(const uint8_t *b, int16_t *w, int n) {
    for (int i = 0; i < n; i++) w[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: interptest DLL CAPTURE\n"); return 2; }
    bst_tables t;
    if (load_tables(&t, argv[1]) < 0) { fprintf(stderr, "table load failed\n"); return 1; }

    FILE *f = fopen(argv[2], "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

    char line[4096];
    int16_t target[BST_ORDER], before[BST_ORDER];
    int dur = 0, rem = 0, have = 0, pass = 0, fail = 0;

    while (fgets(line, sizeof line, f)) {
        char *p = strchr(line, '|');
        if (!p) continue;
        uint8_t b[64];

        if (line[0] == 'B') {
            int16_t tmp[BST_ORDER];
            hexbytes(p + 1, b, 20); words(b, target, BST_ORDER);
            p = strchr(p + 1, '|'); if (!p) continue;
            hexbytes(p + 1, b, 20); words(b, before, BST_ORDER);
            p = strchr(p + 1, '|'); if (!p) continue;
            hexbytes(p + 1, b, 2);  words(b, tmp, 1); dur = tmp[0];
            p = strchr(p + 1, '|'); if (!p) continue;
            hexbytes(p + 1, b, 2);  words(b, tmp, 1); rem = tmp[0];
            have = 1;
        } else if (line[0] == 'A' && have) {
            int16_t after[BST_ORDER];
            hexbytes(p + 1, b, 20); words(b, after, BST_ORDER);

            bst_interp ip;
            bst_interp_init(&ip, &t);
            memcpy(ip.state, before, sizeof before);
            bst_interp_step(&ip, target, dur, rem);

            if (memcmp(ip.state, after, sizeof after) == 0) pass++;
            else {
                if (fail == 0) {
                    fprintf(stderr, "first mismatch, dur %d rem %d\n", dur, rem);
                    for (int i = 0; i < BST_ORDER; i++)
                        fprintf(stderr, "  k%d before %6d target %6d ours %6d theirs %6d\n",
                                i, before[i], target[i], ip.state[i], after[i]);
                }
                fail++;
            }
            have = 0;
        }
    }
    fclose(f);
    printf("interpolation: %d exact, %d differing\n", pass, fail);
    return fail ? 1 : 0;
}
