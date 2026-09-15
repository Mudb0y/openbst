#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"

/* Says its arguments, or its standard input, and writes a wav on standard
   output. With --raw it writes the samples on their own. */

static void wav(FILE *f, long n, int rate) {
    unsigned char h[44] = "RIFF\0\0\0\0WAVEfmt \20\0\0\0\1\0\1\0";
    unsigned long bytes = (unsigned long)n * 2, riff = bytes + 36, brate = (unsigned long)rate * 2;
    for (int i = 0; i < 4; i++) {
        h[4 + i]  = (unsigned char)(riff >> (8 * i));
        h[24 + i] = (unsigned char)((unsigned long)rate >> (8 * i));
        h[28 + i] = (unsigned char)(brate >> (8 * i));
        h[40 + i] = (unsigned char)(bytes >> (8 * i));
    }
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    fwrite(h, 1, 44, f);
}

int main(int argc, char **argv) {
    const char *name = "2006ENG", *dll = NULL;
    int raw = 0, list = 0;
    int pitch = -1, top = -1, level = -1, voice = -1, rate = -1;

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if      (!strcmp(argv[i], "--raw"))   raw = 1;
        else if (!strcmp(argv[i], "--list"))  list = 1;
        else if (i + 1 >= argc)               break;
        else if (!strcmp(argv[i], "--build")) name  = argv[++i];
        else if (!strcmp(argv[i], "--dll"))   dll   = argv[++i];
        else if (!strcmp(argv[i], "--pitch")) pitch = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--top"))   top   = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--level")) level = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--voice")) voice = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--rate"))  rate  = (int)strtol(argv[++i], NULL, 0);
        else break;
    }

    if (list) {
        const char *names[64];
        int n = bst_builds(names, 64);
        for (int k = 0; k < n && k < 64; k++) printf("%s\n", names[k]);
        return 0;
    }

    bst *h;
    if (dll) {
        size_t n = 0;
        FILE *g = fopen(dll, "rb");
        char *d = NULL;
        if (g) {
            fseek(g, 0, SEEK_END);
            long l = ftell(g);
            fseek(g, 0, SEEK_SET);
            d = malloc((size_t)l);
            if (d && fread(d, 1, (size_t)l, g) == (size_t)l) n = (size_t)l;
            fclose(g);
        }
        h = n ? bst_open_image(name, d, n) : NULL;
    } else {
        h = bst_open(name);
    }
    if (!h) {
        fprintf(stderr, "bstspeak: cannot open build %s\n", name);
        return 1;
    }
    if (pitch >= 0) bst_set(h, "pitch", pitch);
    if (top >= 0)   bst_set(h, "top", top);
    if (level >= 0) bst_set(h, "level", level);
    if (voice >= 0) bst_set(h, "voice", voice);
    if (rate >= 0)  bst_set(h, "rate", rate);

    char text[1 << 16];
    size_t n = 0;
    if (i < argc) {
        for (; i < argc && n + 2 < sizeof text; i++) {
            if (n) text[n++] = ' ';
            size_t l = strlen(argv[i]);
            if (n + l + 1 >= sizeof text) l = sizeof text - n - 1;
            memcpy(text + n, argv[i], l);
            n += l;
        }
    } else {
        n = fread(text, 1, sizeof text - 1, stdin);
    }
    text[n] = 0;

    long want = bst_length(h, text);
    if (want < 0) { bst_close(h); return 1; }
    int16_t *pcm = malloc((size_t)(want ? want : 1) * sizeof *pcm);
    if (!pcm) { bst_close(h); return 1; }
    long got = bst_say(h, text, pcm, want);

    if (!raw) wav(stdout, got, bst_rate(h));
    fwrite(pcm, sizeof *pcm, (size_t)got, stdout);
    free(pcm);
    bst_close(h);
    return 0;
}
