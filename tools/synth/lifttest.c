#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"

/* The tables compiled in against the tables in the binary, on more text than
   the lift itself saw. Anything the lift missed shows up here as a word the
   two say differently. */

#define MAX 400000

static int16_t a[MAX], b[MAX];
static long same, diff;
static bst *ha, *hb;

static void check(const char *text) {
    long na = bst_say(ha, text, a, MAX);
    long nb = bst_say(hb, text, b, MAX);
    if (na == nb && na >= 0 && memcmp(a, b, (size_t)na * sizeof *a) == 0) {
        same++;
        return;
    }
    diff++;
    if (diff <= 10) fprintf(stderr, "  differs: %s (%ld vs %ld)\n", text, na, nb);
}

static unsigned seed = 12345;
static unsigned next(void) { seed = seed * 1103515245u + 12345u; return seed >> 16; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: lifttest BUILD DLL [WORDLIST...]\n"); return 2; }

    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "lifttest: cannot read %s\n", argv[2]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *image = malloc((size_t)n);
    if (!image || fread(image, 1, (size_t)n, f) != (size_t)n) return 1;
    fclose(f);

    ha = bst_open(argv[1]);
    hb = bst_open_image(argv[1], image, (size_t)n);
    if (!ha || !hb) { fprintf(stderr, "lifttest: cannot open %s\n", argv[1]); return 1; }

    char t[128];
    for (int i = 3; i < argc; i++) {
        FILE *g = fopen(argv[i], "rb");
        if (!g) continue;
        while (fgets(t, sizeof t, g)) {
            size_t l = strlen(t);
            while (l && (t[l - 1] == '\n' || t[l - 1] == '\r')) t[--l] = 0;
            if (!l) continue;
            check(t);
            t[l] = '.';
            t[l + 1] = 0;
            check(t);
        }
        fclose(g);
    }

    /* Words the lift never saw. */
    for (int i = 0; i < 20000; i++) {
        int len = 2 + (int)(next() % 7);
        for (int k = 0; k < len; k++) t[k] = (char)('a' + next() % 26);
        t[len] = '.';
        t[len + 1] = 0;
        check(t);
    }
    for (int i = 0; i < 400; i++) {
        snprintf(t, sizeof t, "%u.", next() * 7919u);
        check(t);
    }
    check("The quick brown fox jumps over the lazy dog, twice.");
    check("Dr. Smith paid $1,234.56 on 3rd Feb. at 5:30 p.m.");
    check("Why did the chicken cross the road?");

    printf("%-8s %ld identical, %ld differing\n", argv[1], same, diff);
    bst_close(ha);
    bst_close(hb);
    free(image);
    return diff ? 1 : 0;
}
