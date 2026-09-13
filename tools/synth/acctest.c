#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the accent assignment over captured streams and prints the result.
   Input is one line per stream:

       A <len> <flags> <carried> <level> <tail> <hex byte>...

   Output is one hex line per stream, in the same form. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: acctest DLL < streams\n"); return 2; }
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

    char line[8192];
    uint8_t s[1024];

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'A') continue;
        int len = 0, flags = 0, carried = 0, level = 0, tail = -1, used = 0;
        if (sscanf(line + 1, "%d %d %d %d %d %n",
                   &len, &flags, &carried, &level, &tail, &used) < 5) continue;
        const char *p = line + 1 + used;
        int k = 0;
        while (k < (int)sizeof s) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            s[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        if (len > k) len = k;
        bst_accent_state st = { flags, carried, level, tail, 0 };
        bst_accents(&img, s, len, &st);
        for (int i = 0; i < k; i++) printf("%s%02X", i ? " " : "", s[i]);
        putchar('\n');
    }
    free(d);
    return 0;
}
