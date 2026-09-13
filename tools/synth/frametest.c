#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_frames.h"

/* Runs frame generation over a captured record set and prints the frames.

   Input is four lines per utterance:
       V <voice> <rate> <flags> <defexc> <gainbase> <gainadj>
         <segrd> <trnbase> <trnpend> <itord>
       S <hex bytes of the segment records>
       T <hex bytes of the transition records>
       I <hex bytes of the intonation records>
   and a blank line ends it. */

static int hexbytes(const char *p, uint8_t *out, int max) {
    int n = 0;
    while (n < max) {
        unsigned v;
        while (*p == ' ') p++;
        if (sscanf(p, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        p += 2;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: frametest DLL < records\n"); return 2; }
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
    bst_tables_load(&tab, d, n);

    static uint8_t sbuf[0xD0 * 8], tbuf[0x210 * 6], ibuf[0x60 * 6];
    static uint8_t frames[20000 * 16];
    char line[65536];
    int voice = 0, rate = 0, flags = 0, defexc = 0x30, gb = 0x10, ga = -0x12;
    int segrd = 0, trnbase = 0, trnpend = 0, itord = 0;
    int ns = 0, nt = 0, ni = 0;

    while (fgets(line, sizeof line, stdin)) {
        switch (line[0]) {
        case 'V':
            sscanf(line + 1, "%d %d %d %d %d %d %d %d %d %d",
                   &voice, &rate, &flags, &defexc, &gb, &ga,
                   &segrd, &trnbase, &trnpend, &itord);
            break;
        case 'S': ns = hexbytes(line + 1, sbuf, sizeof sbuf) / 8; break;
        case 'T': nt = hexbytes(line + 1, tbuf, sizeof tbuf) / 6; break;
        case 'I': ni = hexbytes(line + 1, ibuf, sizeof ibuf) / 6; break;
        default: {
            if (!ns) break;
            bst_gen g;
            memset(&g, 0, sizeof g);
            g.img = &img;
            g.tab = &tab;
            g.seg = (bst_seg_rec *)sbuf; g.nseg = ns;
            g.trn = (bst_trn_rec *)tbuf; g.ntrn = nt;
            g.ito = (bst_ito_rec *)ibuf; g.nito = ni;
            g.voice = voice;
            g.rate = rate;
            g.rate_loaded = rate;
            g.flags = flags;
            g.defexc = defexc;
            g.gain_base = gb;
            g.gain_adj = ga;
            g.segrd = segrd;
            g.trnbase = trnbase;
            g.trnpend = trnpend;
            g.itord = itord;
            g.out = frames;
            g.maxout = (int)(sizeof frames / 16);
            int m = bst_generate(&g);
            if (m > g.maxout) m = g.maxout;
            printf("#\n");
            for (int i = 0; i < m; i++) {
                for (int k = 0; k < 16; k++)
                    printf("%s%02X", k ? " " : "", frames[i * 16 + k]);
                putchar('\n');
            }
            ns = nt = ni = 0;
            break;
        }
        }
    }
    free(d);
    return 0;
}
