#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_frames.h"

/* Everything after the tokeniser, in our own code: assembly, the rule pass,
   accents, the pair scan, the contour, frame generation and the lattice.

   Input is one line per token,
       K <kind> <emph> <mode> <hex buffer>
   preceded once by
       V <base> <top> <level> <voice> <rate> <gflags> <defexc> <gainbase> <gainadj>

   Output is the raw sixteen-bit samples. */

#define MAXSEG 0xD0
#define MAXTRN 0x210
#define MAXITO 0x60

static bst_seg_rec seg[MAXSEG];
static bst_trn_rec trn[MAXTRN];
static bst_ito_rec ito[MAXITO];
static bst_emit    em[4096];
static bst_contour_rec cr[MAXITO];
static uint8_t frames[80000 * 16];
static uint8_t stream[0x200];

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: fulltest DLL < tokens > pcm\n"); return 2; }
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
    if (bst_tables_load(&tab, d, n) < 0) return 1;

    int base = 0, top = 0, level = 0, vsel = 0, rate = 0;
    int gflags = 0, defexc = 0x30, gb = 0x10, ga = -0x12;
    int nframes = 0, have_voice = 0;

    bst_voice voice;
    memset(&voice, 0, sizeof voice);
    /* Silence is what the first pair starts from. */
    bst_pair_state ps = { 0x30, 0, 0, 0 };
    bst_accent_state as = { 0, 0, 0, -1, 0 };

    bst_assembler z;
    bst_assemble_init(&z, &img, stream);
    memset(stream, 0, sizeof stream);
    bst_assemble_start(&z);

    char line[8192];
    uint8_t buf[256];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'V') {
            sscanf(line + 1, "%d %d %d %d %d %d %d %d %d", &base, &top, &level,
                   &vsel, &rate, &gflags, &defexc, &gb, &ga);
            continue;
        }
        if (line[0] != 'K') continue;
        int kind = 0, emph = 0, mode = 0, used = 0;
        if (sscanf(line + 1, "%d %d %d %n", &kind, &emph, &mode, &used) < 3) continue;
        const char *p = line + 1 + used;
        int k = 0;
        memset(buf, 0, sizeof buf);
        while (k < (int)sizeof buf) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            buf[k++] = (uint8_t)v;
            p += 2;
            while (*p == ' ') p++;
        }
        z.emph = emph;
        z.mode = mode;
        /* Only a phrase the tokens actually closed is spoken; whatever is
           left over when the text ends is dropped, as the engine drops it. */
        if (!bst_assemble_token(&z, kind, buf)) continue;

        int len = z.len > 0 ? z.len : z.wp + 1;
        if (len <= 0) { memset(stream, 0, sizeof stream); bst_assemble_start(&z); continue; }

        /* The rule pass can insert a glottal stop, which moves the header
           the next phrase's level is handed on through. */
        int grew = bst_phrules(&img, stream, &len, (int)sizeof stream, 0);
        z.hdr += grew;

        /* The slot that hands a pitch level to the next phrase is the fourth
           byte of the header the assembler just wrote. */
        as.flags = 0;
        as.level = level;
        as.tail = z.hdr >= 0 ? z.hdr + 3 : -1;
        bst_accents(&img, stream, len, &as);
        level = as.level;
        if (argc > 2 && !strcmp(argv[2], "-s")) {
            for (int i = 0; i < len; i++)
                fprintf(stderr, "%s%02X", i ? " " : "", stream[i]);
            fputc('\n', stderr);
        }

        if (argc > 2 && !strcmp(argv[2], "-s"))
            fprintf(stderr, "state prev %02X strong %02X level %d hdr %d wp %d len %d\n",
                    ps.prev, ps.strong, level, z.hdr, z.wp, len);
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

        if (!have_voice) {
            voice.base = voice.voicebase = base;
            voice.top = top;
            voice.voice = vsel;
            have_voice = 1;
        }
        voice.level = level;
        voice.strong = ps.strong;
        int ni = bst_contour(&img, stream, len, &voice, cr, MAXITO);
        /* The group cursor is carried on from the contour, not from the pair
           scan: both advance it and the contour runs second. */
        ps.strong = voice.strong;
        if (ni > MAXITO) ni = MAXITO;
        for (int i = 0; i < ni; i++) {
            ito[i].kind = cr[i].kind;
            ito[i].period = cr[i].period;
            ito[i].dur = cr[i].dur;
            ito[i].slope = cr[i].slope;
        }

        bst_gen g;
        memset(&g, 0, sizeof g);
        g.img = &img; g.tab = &tab;
        g.seg = seg; g.nseg = ns;
        g.trn = trn; g.ntrn = nt;
        g.ito = ito; g.nito = ni;
        g.voice = vsel; g.rate = rate; g.rate_loaded = rate;
        g.flags = gflags; g.defexc = defexc;
        g.gain_base = gb; g.gain_adj = ga;
        g.out = frames + nframes * 16;
        g.maxout = (int)(sizeof frames / 16) - nframes;
        int m = bst_generate(&g);
        if (m > g.maxout) m = g.maxout;
        nframes += m;

        memset(stream, 0, sizeof stream);
        bst_assemble_start(&z);
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
