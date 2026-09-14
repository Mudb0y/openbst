#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst_synth.h"

/* Renders the frame dump produced by "oracle --frames" using our own
   synthesizer, so the result can be compared sample for sample against the
   audio the original produced for the same text. */

/* `tables`, when given, names the file offsets of the pulse, noise and gain
   tables, and optionally the log pair and the durations. tablescan.py reports
   them for any build. */
static int read_tables(bst_tables *t, const char *dll, const char *tables) {
    FILE *f = fopen(dll, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", dll); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *img = malloc(n);
    if (!img || fread(img, 1, n, f) != (size_t)n) { fclose(f); free(img); return -1; }
    fclose(f);
    int rc;
    if (tables) {
        bst_offsets o = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        size_t lb = 0, os = 0, nk = 0;
        size_t *fields[9] = { &o.pulse, &o.noise, &o.gain, &o.log, &o.alog,
                              &o.duration, &lb, &os, &nk };
        const char *p = tables;
        for (int i = 0; i < 9 && p && *p; i++) {
            char *e = NULL;
            *fields[i] = (size_t)strtoul(p, &e, 16);
            p = (e && *e == ',') ? e + 1 : NULL;
        }
        o.log_bytes = (int)lb;
        o.out_shift = (int)os;
        o.noise_kind = (int)nk;
        rc = bst_tables_load_at(t, img, n, &o);
    } else {
        rc = bst_tables_load(t, img, n);
    }
    free(img);
    return rc;
}

static void write_wav(FILE *f, const int16_t *pcm, size_t n, uint32_t rate) {
    uint32_t datalen = (uint32_t)(n * 2), riff = 36 + datalen, fmtlen = 16;
    uint32_t byterate = rate * 2;
    uint16_t tag = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f);
    fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&datalen, 4, 1, f);
    fwrite(pcm, 2, n, f);
}

int main(int argc, char **argv) {
    const char *dll = NULL, *framefile = NULL, *out = NULL, *tables = NULL;
    int raw = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dll") && i + 1 < argc)         dll = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) framefile = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)    out = argv[++i];
        else if (!strcmp(argv[i], "--tables") && i + 1 < argc) tables = argv[++i];
        else if (!strcmp(argv[i], "--raw")) raw = 1;
        else {
            fprintf(stderr, "usage: synth --dll DLL --frames FILE [--out WAV]"
                            " [--tables pulse,noise,gain] [--raw]\n");
            return 2;
        }
    }
    if (!dll || !framefile) { fprintf(stderr, "need --dll and --frames\n"); return 2; }

    bst_tables t;
    if (read_tables(&t, dll, tables) < 0) { fprintf(stderr, "table load failed\n"); return 1; }

    FILE *ff = strcmp(framefile, "-") ? fopen(framefile, raw ? "rb" : "r") : stdin;
    if (!ff) { fprintf(stderr, "cannot open %s\n", framefile); return 1; }

    bst_synth s;
    bst_synth_init(&s, &t);

    size_t cap = 1 << 20, n = 0;
    int16_t *pcm = malloc(cap * sizeof *pcm);

    char tok[64];
    int nframes = 0;
    /* The 16-bit oracle writes frames as they reach putfr, sixteen raw bytes
       each, rather than in the 32-bit oracle's traced text form. */
    while (raw) {
        uint8_t f[16];
        if (fread(f, 1, 16, ff) != 16) break;
        nframes++;
        if (!bst_synth_frame(&s, f)) continue;
        n += bst_synth_run(&s, pcm + n, cap - n);
    }
    while (!raw && fscanf(ff, "%63s", tok) == 1) {
        if (strcmp(tok, "F") != 0) continue;
        uint8_t f[16];
        int ok = 1;
        for (int i = 0; i < 16; i++) {
            unsigned v;
            if (fscanf(ff, "%2x", &v) != 1) { ok = 0; break; }
            f[i] = (uint8_t)v;
        }
        if (!ok) break;
        nframes++;
        if (!bst_synth_frame(&s, f)) continue;
        n += bst_synth_run(&s, pcm + n, cap - n);
    }
    if (ff != stdin) fclose(ff);

    fprintf(stderr, "%d frames, %zu samples (%.2f s)\n", nframes, n, n / 11025.0);

    if (out) {
        FILE *o = fopen(out, "wb");
        if (!o) { fprintf(stderr, "cannot write %s\n", out); return 1; }
        write_wav(o, pcm, n, 11025);
        fclose(o);
        fprintf(stderr, "wrote %s\n", out);
    }
    free(pcm);
    return 0;
}
