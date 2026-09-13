#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Measures the rule pass against the engine and against the identity, which
   is the bar that matters: the pass rewrites few positions, so doing nothing
   already scores about 95%. */

/* Reads the nth "|"-delimited field of a capture line as hex bytes. */
static int hexfield(const char *l, int field, uint8_t *out, int max) {
    const char *p = l;
    for (int k = 0; k <= field; k++) {
        p = strchr(p, '|');
        if (!p) return -1;
        p++;
    }
    int n = 0;
    while (n < max) {
        unsigned v;
        while (*p == ' ') p++;
        if (sscanf(p, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        p += 2;
    }
    return n;
}

static int hexline(const char *l, uint8_t *out, int max) {
    return hexfield(l, 0, out, max);
}

extern int bst_rule_mask;

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: phtest DLL CAPTURE\n"); return 2; }
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

    if (argc > 3) bst_rule_mask = (int)strtol(argv[3], NULL, 0);
    FILE *g = fopen(argv[2], "r");
    if (!g) return 1;

    char line[1024];
    uint8_t before[512], after[512], ours[512];
    int have = 0, blen = 0;
    int shown = 0;
    long ok = 0, tot = 0, exact = 0, streams = 0, base_ok = 0, base_exact = 0;

    while (fgets(line, sizeof line, g)) {
        if (line[0] == 'B') {
            blen = hexline(line, before, sizeof before);
            uint8_t lb[2];
            if (hexfield(line, 1, lb, 2) == 2) {
                int real = lb[0] | (lb[1] << 8);
                if (real > 0 && real < blen) blen = real;
            }
            have = blen > 0;
        }
        else if (line[0] == 'A' && have) {
            int alen = hexline(line, after, sizeof after);
            have = 0;
            if (alen < blen) continue;
            memcpy(ours, before, (size_t)blen);
            int olen = blen;
            bst_phrules(&img, ours, &olen, (int)sizeof ours, 0);
            streams++;
            int bad = 0, bbad = 0;
            for (int i = 0; i < blen; i++) {
                tot++;
                if (ours[i] == after[i]) ok++; else bad++;
                if (before[i] == after[i]) base_ok++; else bbad++;
            }
            if (!bad) exact++;
            else if (shown < 4) {
                shown++;
                fprintf(stderr, "  stream %ld differs at:", streams);
                for (int i = 0; i < blen; i++)
                    if (ours[i] != after[i])
                        fprintf(stderr, " [%d] ours %02X theirs %02X (was %02X)",
                                i, ours[i], after[i], before[i]);
                fprintf(stderr, "\n");
            }
            if (!bbad) base_exact++;
        }
    }
    fclose(g);
    printf("phtest: ours     %ld/%ld positions, %ld/%ld streams exact\n", ok, tot, exact, streams);
    printf("        identity %ld/%ld positions, %ld/%ld streams\n", base_ok, tot, base_exact, streams);
    printf("        %s\n", (ok >= base_ok && exact >= base_exact)
                           ? "at or ahead of the baseline" : "BELOW the baseline");
    return ok == tot ? 0 : 1;
}
