#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_frames.h"
#include "bst_synth.h"
#include "bst_token.h"

/* The whole engine in our own code: text in, samples out.
 *
 * Tokenise, assemble, apply the rule pass, place the accents, scan the pairs,
 * build the contour, generate the frames, run the lattice. Nothing of the
 * original is used but its tables. */

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
    int want_frames = 0, ne = 0, voice_sel = 1;
    const char *params = NULL;
    const char *tablespec = NULL;
    for (;;) {
        if (argc > 1 && strcmp(argv[1], "--frames") == 0) { want_frames = 1; argv++; argc--; }
        else if (argc > 1 && strcmp(argv[1], "--ne") == 0) { ne = 1; argv++; argc--; }
        else if (argc > 2 && strcmp(argv[1], "--tables") == 0) { tablespec = argv[2]; argv += 2; argc -= 2; }
        else if (argc > 2 && strcmp(argv[1], "--voice") == 0) { voice_sel = atoi(argv[2]); argv += 2; argc -= 2; }
        else if (argc > 2 && strcmp(argv[1], "--params") == 0) { params = argv[2]; argv += 2; argc -= 2; }
        else if (argc > 1 && strcmp(argv[1], "--gaintrace") == 0) { bst_trace = 1; argv++; argc--; }
        else break;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: saytest [--frames] [--ne] [--tables o,o,o,o,o,o]"
                        " DLL TEXT > pcm\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);

    bst_image img;
    if (ne) {
        if (bst_image_init_ne(&img, d, n, &BST_MAP_1998_ENG) < 0) {
            fprintf(stderr, "not a 16-bit module\n");
            return 1;
        }
    } else {
        bst_image_init(&img, d, n);
    }
    bst_tables tab;
    if (tablespec) {
        bst_offsets o = { 0, 0, 0, 0, 0, 0 };
        size_t *fields[6] = { &o.pulse, &o.noise, &o.gain, &o.log, &o.alog, &o.duration };
        const char *q = tablespec;
        for (int i = 0; i < 6 && q && *q; i++) {
            char *e = NULL;
            *fields[i] = (size_t)strtoul(q, &e, 16);
            q = (e && *e == ',') ? e + 1 : NULL;
        }
        if (bst_tables_load_at(&tab, d, n, &o) < 0) return 1;
    } else if (bst_tables_load(&tab, d, n) < 0) {
        return 1;
    }

    /* The voice, as the engine has it before the first word. */
    int base = 0x50, top = 0xA0, level = 3, vsel = voice_sel, rate = 0;
    int gflags = 0, defexc = 0x30, gb = 0x10, ga = -0x12, aflags = 0;
    if (params) {
        int *f[10] = { &base, &top, &level, &vsel, &rate, &gflags, &defexc,
                       &gb, &ga, &aflags };
        const char *q = params;
        for (int i = 0; i < 10 && q && *q; i++) {
            char *e = NULL;
            *f[i] = (int)strtol(q, &e, 0);
            q = (e && *e == ',') ? e + 1 : NULL;
        }
    }
    int nframes = 0, have_voice = 0;

    bst_voice voice;
    memset(&voice, 0, sizeof voice);
    bst_pair_state ps = { 0x30, 0, 0, 0 };
    bst_accent_state as = { 0, 0, 0, -1, 0 };

    bst_tok tk;
    bst_tok_init(&tk, &img, argv[2]);

    bst_assembler z;
    bst_assemble_init(&z, &img, stream);
    memset(stream, 0, sizeof stream);
    bst_assemble_start(&z);

    uint8_t buf[128];
    for (int guard = 0; guard < 4096; guard++) {
        memset(buf, 0, sizeof buf);
        int kind = bst_tok_next(&tk, buf);
        z.emph = 0;
        z.mode = 0;
        int done = bst_assemble_token(&z, kind, buf);
        if (!done) { if (kind == 6) break; else continue; }

        int len = z.len > 0 ? z.len : z.wp + 1;
        if (len <= 0) { memset(stream, 0, sizeof stream); bst_assemble_start(&z); continue; }

        int grew = bst_phrules(&img, stream, &len, (int)sizeof stream, 0);
        z.hdr += grew;

        as.flags = aflags;
        as.level = level;
        as.tail = z.hdr >= 0 ? z.hdr + 3 : -1;
        bst_accents(&img, stream, len, &as);
        level = as.level;

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

        if (bst_trace) {
            fprintf(stderr, "stream");
            for (int i = 0; i < len; i++) fprintf(stderr, " %02x", stream[i]);
            fprintf(stderr, "\ntrn");
            for (int i = 0; i < nt; i++)
                fprintf(stderr, " %02x/%02x", trn[i].index, trn[i].dur);
            fprintf(stderr, "\nseg");
            for (int i = 0; i < ns; i++)
                fprintf(stderr, " %u:%d", seg[i].index, seg[i].count);
            fprintf(stderr, "\n");
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
        if (ni > MAXITO) ni = MAXITO;
        ps.strong = voice.strong;
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
        if (kind == 6) break;
    }

    if (want_frames) {
        fwrite(frames, 16, nframes, stdout);
        free(d);
        return 0;
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
