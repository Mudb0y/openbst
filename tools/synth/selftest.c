#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"

/* The library against what it said when the tables were lifted, with no
 * original binary anywhere.
 *
 * The other tests all compare against an original running under the emulator,
 * so none of them can be run by anyone who does not have the binaries. This
 * one carries the answers instead: tests/golden.txt holds, for every build
 * and every utterance, how many samples came out and a hash of them. Those
 * numbers were written by --write, which reads the tables out of the binaries
 * rather than the ones compiled in, so they are the originals' answers and
 * not the library's own.
 *
 * What it catches is a change that moves the output. What it cannot catch is
 * the two of them being wrong together, which is what the emulator tests are
 * for. */

#define MAX 800000

static int16_t pcm[MAX];

static unsigned long long hash(const int16_t *p, long n) {
    unsigned long long h = 14695981039346656037ULL;
    const unsigned char *b = (const unsigned char *)p;
    for (long i = 0; i < n * 2; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The utterances a build is put through: every word of its corpus on its own
   and again with a stop after it, a spread of numbers, and two sentences. */
static const char *NUMBERS[] = {
    "0", "1", "2", "3", "7", "11", "21", "40", "100", "101", "1000",
    "12345", "1000000",
};
static const char *SENTENCES[] = {
    "The quick brown fox jumps over the lazy dog.",
    "Hello there, how are you?",
};

static void emit(FILE *out, bst *h, const char *name, const char *text) {
    long n = bst_say(h, text, pcm, MAX);
    if (n < 0) n = 0;
    fprintf(out, "%s\t%ld\t%016llx\t%s\n", name, n, hash(pcm, n), text);
}

static int write_one(FILE *out, const char *name, const char *dll,
                     char **lists, int nlists) {
    FILE *f = fopen(dll, "rb");
    if (!f) { fprintf(stderr, "selftest: cannot read %s\n", dll); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *image = malloc((size_t)len);
    if (!image || fread(image, 1, (size_t)len, f) != (size_t)len) return 1;
    fclose(f);

    bst *h = bst_open_image(name, image, (size_t)len);
    if (!h) { fprintf(stderr, "selftest: cannot open %s\n", name); return 1; }

    char word[256];
    for (int i = 0; i < nlists; i++) {
        FILE *g = fopen(lists[i], "rb");
        if (!g) continue;
        int c, w = 0;
        for (;;) {
            c = fgetc(g);
            if (c != EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                if (w + 2 < (int)sizeof word) word[w++] = (char)c;
                continue;
            }
            if (w) {
                word[w] = 0;
                emit(out, h, name, word);
                word[w] = '.';
                word[w + 1] = 0;
                emit(out, h, name, word);
            }
            w = 0;
            if (c == EOF) break;
        }
        fclose(g);
    }
    for (size_t i = 0; i < sizeof NUMBERS / sizeof NUMBERS[0]; i++)
        emit(out, h, name, NUMBERS[i]);
    for (size_t i = 0; i < sizeof SENTENCES / sizeof SENTENCES[0]; i++)
        emit(out, h, name, SENTENCES[i]);

    bst_close(h);
    free(image);
    return 0;
}

static int check(const char *golden) {
    FILE *f = fopen(golden, "rb");
    if (!f) { fprintf(stderr, "selftest: cannot read %s\n", golden); return 1; }

    char line[1024], open_as[64] = "";
    bst *h = NULL;
    long same = 0, diff = 0, bad = 0;

    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l) continue;

        char *name = line;
        char *t1 = strchr(name, '\t');
        if (!t1) { bad++; continue; }
        *t1 = 0;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) { bad++; continue; }
        *t2 = 0;
        char *t3 = strchr(t2 + 1, '\t');
        if (!t3) { bad++; continue; }
        *t3 = 0;
        long want_n = strtol(t1 + 1, NULL, 10);
        unsigned long long want_h = strtoull(t2 + 1, NULL, 16);
        const char *text = t3 + 1;

        if (strcmp(open_as, name) != 0) {
            bst_close(h);
            h = bst_open(name);
            if (!h) {
                fprintf(stderr, "selftest: no build named %s\n", name);
                fclose(f);
                return 1;
            }
            snprintf(open_as, sizeof open_as, "%s", name);
        }

        long n = bst_say(h, text, pcm, MAX);
        if (n == want_n && hash(pcm, n) == want_h) {
            same++;
        } else {
            if (diff < 10)
                fprintf(stderr, "  %s differs: %s (%ld samples, wanted %ld)\n",
                        name, text, n, want_n);
            diff++;
        }
    }
    bst_close(h);
    fclose(f);

    if (bad) fprintf(stderr, "  %ld unreadable lines\n", bad);
    printf("selftest: %ld identical, %ld differing\n", same, diff);
    return (diff || bad || !same) ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "--write") == 0)
        return write_one(stdout, argv[2], argv[3], argv + 4, argc - 4);
    if (argc == 2)
        return check(argv[1]);
    fprintf(stderr, "usage: selftest GOLDEN\n"
                    "       selftest --write BUILD DLL [WORDLIST...] > lines\n");
    return 2;
}
