#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the stress pass over captured word streams. Input is one line each:

       W <emph> <mode> <hex bytes of the buffer, length byte first>

   Output is the buffer after the pass, in the same form. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: stresstest DLL < words\n"); return 2; }
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
    uint8_t buf[256];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'W') continue;
        int emph = 0, mode = 0, used = 0;
        if (sscanf(line + 1, "%d %d %n", &emph, &mode, &used) < 2) continue;
        const char *p = line + 1 + used;
        int k = 0;
        while (k < (int)sizeof buf) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            buf[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        if (k < 3) continue;
        int len = buf[0];
        if (len > k - 2) len = k - 2;
        bst_word_stress(&img, buf + 2, len, emph, mode);
        for (int i = 0; i < k; i++) printf("%s%02X", i ? " " : "", buf[i]);
        putchar('\n');
    }
    free(d);
    return 0;
}
