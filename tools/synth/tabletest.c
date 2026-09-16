#include <stdio.h>
#include <string.h>
#include "bst.h"
#include "bst_priv.h"

/* Every build's lattice tables against the 1995 build's.
 *
 * The excitation, the gain and the log pair are the same numbers in all
 * twenty builds: only where they sit in the file differs, and the 2006 builds
 * keep the log pair a byte to an entry where the earlier ones keep a word.
 * So once they are loaded they must all agree, and a build whose offsets are
 * wrong reads something that does not.
 *
 * This is here because that is exactly what happened. The six 1998 builds
 * were reading their excitation and gain tables out of the language module,
 * where those tables are not: they live in the core module all six share.
 * What came out was a quarter of every sample pinned at full scale, and
 * nothing caught it, because ne98test compares frames and never runs the
 * lattice, and the two tests that do run it compare the library against
 * itself. */

static int fail;

static void cmp(const char *build, const char *what,
                const void *got, const void *want, size_t n) {
    if (memcmp(got, want, n) == 0) return;
    const unsigned char *a = got, *b = want;
    size_t i = 0;
    while (i < n && a[i] == b[i]) i++;
    fprintf(stderr, "  %s %s differs at byte %zu\n", build, what, i);
    fail++;
}

int main(void) {
    bst *ref = bst_open("1995");
    if (!ref) { fprintf(stderr, "tabletest: cannot open 1995\n"); return 1; }
    bst_tables want;
    bst_handle_tables(ref, &want);

    const char *names[64];
    int n = bst_builds(names, 64);
    if (n > 64) n = 64;

    int checked = 0;
    for (int i = 0; i < n; i++) {
        bst *h = bst_open(names[i]);
        if (!h) { fprintf(stderr, "  %s will not open\n", names[i]); fail++; continue; }
        bst_tables t;
        bst_handle_tables(h, &t);
        cmp(names[i], "pulse", t.pulse, want.pulse, sizeof t.pulse);
        cmp(names[i], "noise", t.noise, want.noise, sizeof t.noise);
        cmp(names[i], "gain",  t.gain,  want.gain,  sizeof t.gain);
        cmp(names[i], "log",   t.log,   want.log,   sizeof t.log);
        cmp(names[i], "alog",  t.alog,  want.alog,  sizeof t.alog);
        bst_close(h);
        checked++;
    }
    bst_close(ref);

    printf("tabletest: %d builds checked, %d tables differing\n", checked, fail);
    return fail ? 1 : 0;
}
