#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Runs the contour generator over captured streams. Input is one line each:

       C <len> <base> <top> <level> <voice> <hex byte>...

   Output is a "#" line per stream followed by its records. */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: contest DLL < streams\n"); return 2; }
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
    bst_contour_rec rec[256];

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'C') continue;
        int len = 0, base = 0, top = 0, level = 0, voice = 0, used = 0;
        if (sscanf(line + 1, "%d %d %d %d %d %n",
                   &len, &base, &top, &level, &voice, &used) < 5) continue;
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
        bst_voice pit;
        memset(&pit, 0, sizeof pit);
        pit.base = pit.voicebase = base;
        pit.top = top;
        pit.level = level;
        pit.voice = voice;
        int m = bst_contour(&img, s, len, &pit, rec,
                            (int)(sizeof rec / sizeof rec[0]));
        printf("#\n");
        for (int i = 0; i < m && i < (int)(sizeof rec / sizeof rec[0]); i++)
            printf("%u %u %d %d\n", rec[i].kind, rec[i].period,
                   rec[i].dur, rec[i].slope);
    }
    free(d);
    return 0;
}
