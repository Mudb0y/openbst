#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_frames.h"

/* The whole chain in our own code: a sentence phoneme stream in, raw samples
   out. Accents, the pair scan, the contour, frame generation and the lattice,
   with nothing from the engine but its tables.

   Input is one line per phrase:

       P <len> <flags> <carried> <level> <tail> <base> <top> <voice> <rate>
         <gflags> <defexc> <gainbase> <gainadj> <prev> <strong> <hex bytes>...

   Output is the raw sixteen-bit samples on stdout. */

#define MAXSEG 0xD0
#define MAXTRN 0x210
#define MAXITO 0x60

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: pipetest DLL < phrases > pcm\n"); return 2; }
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
    bst_tables tab;
    if (bst_tables_load(&tab, d, n) < 0) { fprintf(stderr, "tables\n"); return 1; }

    static uint8_t frames[60000 * 16];
    int nframes = 0;

    static bst_seg_rec seg[MAXSEG];
    static bst_trn_rec trn[MAXTRN];
    static bst_ito_rec ito[MAXITO];
    static bst_emit    em[4096];
    static bst_contour_rec cr[MAXITO];
    static uint8_t stream[1024];
    char line[16384];

    bst_voice voice;
    memset(&voice, 0, sizeof voice);
    bst_pair_state ps = { 0, 0, 0, 0 };
    bst_accent_state as = { 0, 0, 0, -1, 0 };
    int have_voice = 0;

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'P') continue;
        int len, flags, carried, level, tail, base, top, vsel, rate;
        int gflags, defexc, gb, ga, prev, strong, used = 0;
        if (sscanf(line + 1, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %n",
                   &len, &flags, &carried, &level, &tail, &base, &top, &vsel,
                   &rate, &gflags, &defexc, &gb, &ga, &prev, &strong, &used) < 15)
            continue;
        const char *p = line + 1 + used;
        int k = 0;
        while (k < (int)sizeof stream) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            stream[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        if (len > k) len = k;

        as.flags = flags;
        as.carried = carried;
        as.level = level;
        as.tail = tail;
        bst_accents(&img, stream, len, &as);

        ps.prev = prev;
        ps.strong = strong;
        int ne = bst_pairs(&img, stream, len, &ps, em,
                           (int)(sizeof em / sizeof em[0]));
        if (ne < 0) ne = (int)(sizeof em / sizeof em[0]);

        int ns = 0, nt = 0;
        for (int i = 0; i < ne; i++) {
            if (em[i].kind == BST_EMIT_SEG) {
                if (ns >= MAXSEG) break;
                seg[ns].count = (int16_t)em[i].count;
                seg[ns].index = em[i].index;
                if (em[i].index & 0xF000) { seg[ns].a = em[i].a; seg[ns].b = em[i].b; }
                else                      { seg[ns].a = -1;      seg[ns].b = -1; }
                ns++;
            } else {
                if (nt >= MAXTRN) break;
                trn[nt].index = (uint8_t)em[i].index;
                trn[nt].dur = (uint8_t)em[i].count;
                trn[nt].p1 = (uint8_t)em[i].a;
                trn[nt].p2 = (uint8_t)em[i].b;
                trn[nt].span = -1;
                nt++;
            }
        }

        /* The voice's range carries between phrases; its floor does not
           change unless a command says so. */
        if (!have_voice) {
            voice.base = voice.voicebase = base;
            voice.top = top;
            voice.voice = vsel;
            have_voice = 1;
        }
        voice.level = level;
        voice.strong = ps.strong;
        int ni = bst_contour(&img, stream, len, &voice, cr, MAXITO);
        if (ni > MAXITO) ni = MAXITO;
        for (int i = 0; i < ni; i++) {
            ito[i].kind = cr[i].kind;
            ito[i].period = cr[i].period;
            ito[i].dur = cr[i].dur;
            ito[i].slope = cr[i].slope;
        }

        bst_gen g;
        memset(&g, 0, sizeof g);
        g.img = &img;
        g.tab = &tab;
        g.seg = seg; g.nseg = ns;
        g.trn = trn; g.ntrn = nt;
        g.ito = ito; g.nito = ni;
        g.voice = vsel;
        g.rate = rate;
        g.rate_loaded = rate;
        g.flags = gflags;
        g.defexc = defexc;
        g.gain_base = gb;
        g.gain_adj = ga;
        g.out = frames + nframes * 16;
        g.maxout = (int)(sizeof frames / 16) - nframes;
        int m = bst_generate(&g);
        if (m > g.maxout) m = g.maxout;
        nframes += m;
    }

    bst_synth s;
    bst_synth_init(&s, &tab);
    size_t cap = 1 << 22, used = 0;
    int16_t *pcm = malloc(cap * sizeof *pcm);
    for (int i = 0; i < nframes; i++) {
        if (!bst_synth_frame(&s, frames + i * 16)) continue;
        used += bst_synth_run(&s, pcm + used, cap - used);
    }
    fwrite(pcm, sizeof *pcm, used, stdout);
    free(pcm);
    free(d);
    return 0;
}
