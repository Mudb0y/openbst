#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_text.h"

/* Verifies the rule pass cursors alone, with no rules applied.
 *
 * Two attempts at the rule pass failed by firing in the wrong places, and the
 * cursors were the suspect rather than the conditions. This checks them
 * directly against the engine's, position by position, so the foundation is
 * known good before any rule is written on top of it. */

#define RDATA_VA  0x10020000u
#define RDATA_OFF 0x17800u
#define PH_ATTR2  0x100216C8u
#define CMD 0x7C

static int a2(const bst_image *img, int c) {
    size_t o = RDATA_OFF + (PH_ATTR2 - RDATA_VA) + (unsigned)(c & 0xFF);
    return o < img->len ? img->image[o] : 0;
}

typedef struct { int val, pos; } cur;

/* bit selects the class; end_markers is set for the scan that also stops on a
   phrase terminator. */
static void scan(const bst_image *img, const uint8_t *s, int len, int from,
                 int bit, int end_markers, int sub, cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p >= len) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (a2(img, b) & bit) { c->val = b - sub; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= len - 1 || (end_markers && (b == 0x4D || b == 0x4E))) {
            c->val = 0; c->pos = 0; return;
        }
    }
}

static int hexfield(const char *l, int idx, uint8_t *out, int n) {
    const char *p = l;
    for (int i = 0; i <= idx; i++) { p = strchr(p, '|'); if (!p) return -1; p++; }
    for (int i = 0; i < n; i++) {
        unsigned v;
        while (*p == ' ') p++;
        if (sscanf(p, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
        p += 2;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: curtest DLL STREAMS CURSORS\n"); return 2; }
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

    /* One stream per line of the corpus, then all the cursor rows in order. */
    FILE *sf = fopen(argv[2], "r"), *cf = fopen(argv[3], "r");
    if (!sf || !cf) return 1;

    char line[1024];
    uint8_t s[256];
    int slen = 0;
    long ok = 0, bad = 0, feedback = 0;

    while (fgets(line, sizeof line, sf)) {
        if (line[0] != 'B') continue;
        const char *p = strchr(line, '|');
        if (!p) continue;
        p++;
        slen = 0;
        while (slen < (int)sizeof s) {
            unsigned v;
            while (*p == ' ') p++;
            if (sscanf(p, "%2x", &v) != 1) break;
            s[slen++] = (uint8_t)v;
            p += 2;
        }

        cur next, next2, stress, eight, pend = {0, 0}, prev = {0, 0};
        scan(&img, s, slen, 0, 1, 1, 0, &next);
        scan(&img, s, slen, next.pos, 1, 1, 0, &next2);
        scan(&img, s, slen, -1, 8, 0, 0, &eight);
        scan(&img, s, slen, 0, 2, 0, 0x30, &stress);

        for (int i = 0; i < slen; i++) {
            if (i == stress.pos) scan(&img, s, slen, i, 2, 0, 0x30, &stress);
            if (pend.pos - i == -1) prev = pend;
            if (i == next.pos) {
                pend = next;
                next = next2;
                scan(&img, s, slen, next2.pos, 1, 1, 0, &next2);
            }
            if (i == eight.pos) scan(&img, s, slen, i, 8, 0, 0, &eight);

            /* The engine samples at every position, empty slots and command
               records included, so a row must be consumed for each. */
            if (!fgets(line, sizeof line, cf) || line[0] != 'C') goto done;
            uint8_t nv[1], np[2], pv[1], sv[1], sp[2], ev[1], ep[2];
            if (hexfield(line, 0, nv, 1) || hexfield(line, 1, np, 2) ||
                hexfield(line, 2, pv, 1) || hexfield(line, 3, sv, 1) ||
                hexfield(line, 4, sp, 2) || hexfield(line, 5, ev, 1) ||
                hexfield(line, 6, ep, 2)) continue;

            int enp = np[0] | (np[1] << 8), esp = sp[0] | (sp[1] << 8);
            int eep = ep[0] | (ep[1] << 8);
            /* prev is deliberately excluded: the pass rewrites the stream as
               it goes and the previous-segment cursor picks up the rewritten
               value, so it cannot be checked without the rules in place. That
               feedback is itself the finding -- cursors and rules interleave
               and cannot be built in separate passes. */
            int match = next.val == nv[0] && next.pos == enp &&
                        stress.val == (int8_t)sv[0] && stress.pos == esp &&
                        eight.val == ev[0] && eight.pos == eep;
            if (prev.val != pv[0]) feedback++;
            if (s[i] == CMD) { i += 6; }
            if (match) ok++;
            else {
                if (bad < 4)
                    fprintf(stderr,
                        "  pos %d: next %02X@%d/%02X@%d stress %d@%d/%d@%d eight %02X@%d/%02X@%d\n",
                        i, next.val, next.pos, nv[0], enp,
                        stress.val, stress.pos, (int8_t)sv[0], esp,
                        eight.val, eight.pos, ev[0], eep);
                bad++;
            }
        }
    }
done:
    fclose(sf); fclose(cf);
    printf("curtest: %ld cursor states match, %ld differ"
           " (%ld previous-cursor values shifted by the pass's own rewrites)\n",
           ok, bad, feedback);
    return bad ? 1 : 0;
}
