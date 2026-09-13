#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the pair scan over captured stream snapshots and prints the records it
   emits. Input is one line per snapshot:

       P <len> <prev> <strong> <hex byte>...

   Output is a "#" line per snapshot followed by its records:

       S <index> <count>
       T <index> <duration> <pitch1> <pitch2>

   With -t, prints instead the acoustic targets the segments expand to, one
   line of indices per snapshot, which is what the frame builder fetches. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: pairtest DLL < snapshots\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    bst_image img;
    bst_image_init(&img, d, n);

    char line[4096];
    uint8_t stream[1024];
    bst_emit rec[2048];

    int targets = argc > 2 && !strcmp(argv[2], "-t");

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'P') continue;
        int len = 0, prev = 0, strong = 0, used = 0;
        if (sscanf(line + 1, "%d %d %d %n", &len, &prev, &strong, &used) < 3) continue;
        const char *p = line + 1 + used;
        int k = 0;
        while (k < (int)sizeof stream) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            stream[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        if (len > k) len = k;
        bst_pair_state st = { prev, strong, 0, 0 };
        int m = bst_pairs(&img, stream, len, &st, rec,
                          (int)(sizeof rec / sizeof rec[0]));
        printf("#\n");
        if (targets) {
            uint16_t t[4096];
            for (int i = 0; i < m; i++) {
                if (rec[i].kind != BST_EMIT_SEG) continue;
                /* A command record is not an allophone and expands to
                   nothing. */
                if (rec[i].index & 0xF000) continue;
                int nt = bst_diphone(&img, rec[i].index, rec[i].count,
                                     t, (int)(sizeof t / sizeof t[0]));
                for (int j = 0; j < nt; j++) printf("%u\n", t[j]);
            }
            continue;
        }
        for (int i = 0; i < m; i++) {
            if (rec[i].kind == BST_EMIT_SEG)
                printf("S %u %u\n", rec[i].index, rec[i].count);
            else
                printf("T %u %u %d %d\n", rec[i].index, rec[i].count,
                       rec[i].a, rec[i].b);
        }
    }
    free(d);
    return 0;
}
