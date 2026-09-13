#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Normalises each word on stdin and prints the resulting buffer, so the shell
   test can compare it against the engine's own. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: normtest DLL < words\n"); return 2; }
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

    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        bst_word w;
        bst_normalise(&img, line, &w);
        printf("%s %s %02x\n", line, w.buf, w.flags);
    }
    free(d);
    return 0;
}
