#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "corpus.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n + 1);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
    fclose(f);
    d[n] = 0;
    if (len) *len = (size_t)n;
    return d;
}

/* Every word of a file, one utterance each. */
void bst_corpus_file(bst *h, const char *path) {
    size_t n;
    char *d = slurp(path, &n);
    if (!d) return;
    char word[256];
    size_t w = 0;
    for (size_t i = 0; i <= n; i++) {
        int c = (unsigned char)d[i];
        if (i < n && c != '\n' && c != '\r' && w + 2 < sizeof word) {
            word[w++] = (char)c;
            continue;
        }
        if (w) {
            word[w] = 0;
            bst_length(h, word);
            word[w] = '.';
            word[w + 1] = 0;
            bst_length(h, word);
        }
        w = 0;
    }
    free(d);
}

/* The letters of a build's own alphabet, every one in every position of a
   one, two and three letter word, which is what walks the rule chains. */
void bst_corpus_sweep(bst *h) {
    static const int lo = 0x20, hi = 0xFF;
    char t[8];
    for (int a = lo; a <= hi; a++) {
        t[0] = (char)a; t[1] = '.'; t[2] = 0;
        bst_length(h, t);
    }
    for (int a = 'a'; a <= 'z'; a++)
        for (int b = 'a'; b <= 'z'; b++) {
            t[0] = (char)a; t[1] = (char)b; t[2] = '.'; t[3] = 0;
            bst_length(h, t);
        }
    for (int a = 'a'; a <= 'z'; a++)
        for (int b = 'a'; b <= 'z'; b++)
            for (int c = 'a'; c <= 'z'; c++) {
                t[0] = (char)a; t[1] = (char)b; t[2] = (char)c; t[3] = '.'; t[4] = 0;
                bst_length(h, t);
            }
    for (int n = 0; n < 2000; n++) {
        char num[16];
        snprintf(num, sizeof num, "%d.", n * 37 + n / 3);
        bst_length(h, num);
    }
    bst_length(h, "Dr. Smith paid $1,234.56 on 3rd Feb. at 5:30 p.m.");
    bst_length(h, "i.e. e.g. etc. vs. Mr. Mrs. Ms. St. Ave.");
    bst_length(h, "The quick brown fox jumps over the lazy dog!");
    bst_length(h, "Why did the chicken cross the road?");
}
