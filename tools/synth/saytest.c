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
    const char *params = NULL, *mapname = NULL;
    const char *tablespec = NULL;
    for (;;) {
        if (argc > 1 && strcmp(argv[1], "--frames") == 0) { want_frames = 1; argv++; argc--; }
        else if (argc > 1 && strcmp(argv[1], "--ne") == 0) { ne = 1; argv++; argc--; }
        else if (argc > 2 && strcmp(argv[1], "--tables") == 0) { tablespec = argv[2]; argv += 2; argc -= 2; }
        else if (argc > 2 && strcmp(argv[1], "--voice") == 0) { voice_sel = atoi(argv[2]); argv += 2; argc -= 2; }
        else if (argc > 2 && strcmp(argv[1], "--params") == 0) { params = argv[2]; argv += 2; argc -= 2; }
        else if (argc > 2 && strcmp(argv[1], "--map") == 0) { mapname = argv[2]; argv += 2; argc -= 2; }
        else if (argc > 1 && strcmp(argv[1], "--trace") == 0) { bst_trace = 1; argv++; argc--; }
        else break;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: saytest [--frames] [--ne] [--tables o,o,o,o,o,o]"
                        " [--trace] DLL TEXT > pcm\n");
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

    static const struct { const char *name; const bst_tabmap *map; } MAPS[] = {
        { "ENG", &BST_MAP_1998_ENG }, { "DUT", &BST_MAP_1998_DUT },
        { "FRN", &BST_MAP_1998_FRN }, { "GRM", &BST_MAP_1998_GRM },
        { "ITL", &BST_MAP_1998_ITL }, { "SPN", &BST_MAP_1998_SPN },
        { "2006ENG", &BST_MAP_2006_ENG },
        { "2006ARA", &BST_MAP_2006_ARA },
        { "2006DUT", &BST_MAP_2006_DUT },
        { "2006FRE", &BST_MAP_2006_FRE },
        { "2006GER", &BST_MAP_2006_GER },
        { "2006GRE", &BST_MAP_2006_GRE },
        { "2006HEB", &BST_MAP_2006_HEB },
        { "2006ITA", &BST_MAP_2006_ITA },
        { "2006JPN", &BST_MAP_2006_JPN },
        { "2006POL", &BST_MAP_2006_POL },
        { "2006POR", &BST_MAP_2006_POR },
        { "2006RUS", &BST_MAP_2006_RUS },
        { "2006SPA", &BST_MAP_2006_SPA },
    };
    const bst_tabmap *map = &BST_MAP_1998_ENG;
    if (mapname) {
        map = NULL;
        for (size_t i = 0; i < sizeof MAPS / sizeof MAPS[0]; i++)
            if (strcmp(MAPS[i].name, mapname) == 0) map = MAPS[i].map;
        if (!map) { fprintf(stderr, "no map named %s\n", mapname); return 2; }
    }

    bst_image img;
    if (ne) {
        if (bst_image_init_ne(&img, d, n, map) < 0) {
            fprintf(stderr, "not a 16-bit module\n");
            return 1;
        }
    } else if (mapname) {
        bst_image_init_map(&img, d, n, map);
    } else {
        bst_image_init(&img, d, n);
    }
    bst_tables tab;
    if (tablespec) {
        bst_offsets o = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        size_t lb = 0, os = 0, nk = 0;
        size_t *fields[9] = { &o.pulse, &o.noise, &o.gain, &o.log, &o.alog,
                              &o.duration, &lb, &os, &nk };
        const char *q = tablespec;
        for (int i = 0; i < 9 && q && *q; i++) {
            char *e = NULL;
            *fields[i] = (size_t)strtoul(q, &e, 16);
            q = (e && *e == ',') ? e + 1 : NULL;
        }
        o.log_bytes = (int)lb;
        o.out_shift = (int)os;
        o.noise_kind = (int)nk;
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
    if (img.t.init_level) level = img.t.init_level;

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
    int nphrase = 0;
    for (int guard = 0; guard < 4096; guard++) {
        memset(buf, 0, sizeof buf);
        int kind = bst_tok_next(&tk, buf);
        if (bst_trace) {
            fprintf(stderr, "tok kind=%d len=%d:", kind, buf[0]);
            for (int i = 0; i < buf[0] + 2 && i < 128; i++) fprintf(stderr, " %02x", buf[i]);
            fprintf(stderr, "\n");
        }
        z.emph = 0;
        z.mode = 0;
        int done = bst_assemble_token(&z, kind, buf);
        if (!done) { if (kind == 6) break; else continue; }

        int len = z.len > 0 ? z.len : z.wp + 1;
        if (len <= 0) { memset(stream, 0, sizeof stream); bst_assemble_start(&z); continue; }

        if (map && map->no_closing_phrase && nphrase > 0) {
            int spoken = 0;
            for (int i = 12; i < len; i++)
                if (stream[i] && stream[i] < 0x2F) { spoken = 1; break; }
            /* The build says the phrase the closing brace opens only when
               the sentence ended on something other than a full stop. A
               first phrase with nothing in it is still said, as the silence
               it is. */
            if (!spoken && (tk.lastend == '.' || tk.lastend == 0)) break;
        }
        nphrase++;

        if (bst_trace) {
            fprintf(stderr, "assembled");
            for (int i = 0; i < len; i++) fprintf(stderr, " %02x", stream[i]);
            fprintf(stderr, "\n");
        }
        int grew = bst_phrules(&img, stream, &len, (int)sizeof stream, 0);
        if (bst_trace) {
            fprintf(stderr, "ruled    ");
            for (int i = 0; i < len; i++) fprintf(stderr, " %02x", stream[i]);
            fprintf(stderr, "\n");
        }
        z.hdr += grew;

        as.flags = aflags;
        as.level = level;
        as.tail = z.hdr >= 0 ? z.hdr + 3 : -1;
        bst_accents(&img, stream, len, &as);
        level = as.level;

        if (bst_trace) {
            fprintf(stderr, "atpairs");
            for (int i = 0; i < len; i++) fprintf(stderr, " %02x", stream[i]);
            fprintf(stderr, "\n");
        }
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
                fprintf(stderr, " %02x:%02x,%02x,%02x", trn[i].index, trn[i].dur,
                        trn[i].p1, trn[i].p2);
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

        if (bst_trace) {
            fprintf(stderr, "ito n=%d:", ni);
            for (int i = 0; i < ni; i++)
                fprintf(stderr, " %d/%d/%d/%d", ito[i].kind, ito[i].period,
                        ito[i].dur, ito[i].slope);
            fprintf(stderr, "\n");
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
