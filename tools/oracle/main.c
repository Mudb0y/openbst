#include <stdlib.h>
#include <string.h>
#include "winemu.h"
#include "profile.h"

static void usage(void) {
    fprintf(stderr,
        "usage: oracle --dll PATH [options]\n"
        "  --call NAME[:ARG,ARG,...]   call an export; repeatable, in order\n"
        "  --out FILE                  write captured audio (wav unless --raw)\n"
        "  --raw                       write headerless pcm\n"
        "  --limit N                   instruction budget per call\n"
        "  --poke ADDR=VAL[:W]         write VAL (width W bytes, default 4) before calls\n"
        "  --dump ADDR:LEN[:FILE]      dump guest memory after the calls\n"
        "  --trace FILE                log every read from the image\n"
        "  --list                      list exports and exit\n"
        "\n"
        "high-level modes (1995 build):\n"
        "  --speak TEXT                synthesize and capture audio\n"
        "  --phonemes TEXT             print the engine's phoneme transcription\n"
        "  --frames TEXT               print the 16-byte synthesizer parameter frames\n"
        "  --records TEXT              print the segment records driving frame generation\n"
        "  --interp TEXT               print the interpolator state at each frame\n"
        "  --level N                   verbosity for --phonemes (default 6)\n"
        "  --watch ADDR:LEN[:FILE]     log writes into an address range\n"
        "  --hook ADDR[:NARGS]         log stack arguments at a function entry\n"
        "  -v                          verbose\n"
        "\n"
        "argument forms: 12345 | 0xabc | str:TEXT | wstr:TEXT | buf:N | ptr:N\n"
        "  buf:N allocates N zeroed bytes and passes its address, then dumps it\n"
        "  ptr:N allocates N zeroed bytes and passes its address quietly\n");
}

struct outbuf { uint32_t addr; uint32_t len; };

static int parse_arg(emu *e, const char *s, uint32_t *out, struct outbuf *ob) {
    if (strncmp(s, "str:", 4) == 0) { *out = emu_push_str(e, s + 4); return 0; }
    if (strncmp(s, "wstr:", 5) == 0) {
        const char *t = s + 5;
        size_t n = strlen(t);
        uint16_t *w = calloc(n + 1, 2);
        for (size_t i = 0; i < n; i++) w[i] = (uint8_t)t[i];
        *out = emu_push_bytes(e, w, (uint32_t)(n + 1) * 2);
        free(w);
        return 0;
    }
    if (strncmp(s, "buf:", 4) == 0 || strncmp(s, "ptr:", 4) == 0) {
        uint32_t n = (uint32_t)strtoul(s + 4, NULL, 0);
        *out = emu_alloc(e, n);
        if (s[0] == 'b' && ob) { ob->addr = *out; ob->len = n; }
        return 0;
    }
    *out = (uint32_t)strtoul(s, NULL, 0);
    return 0;
}

static void write_wav(FILE *f, emu *e) {
    uint32_t datalen = (uint32_t)e->pcm_len;
    uint32_t byterate = e->sample_rate * e->channels * (e->bits / 8);
    uint16_t blockalign = (uint16_t)(e->channels * (e->bits / 8));
    uint32_t riff = 36 + datalen;
    uint32_t fmtlen = 16;
    uint16_t pcmtag = 1;

    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f);
    fwrite(&pcmtag, 2, 1, f); fwrite(&e->channels, 2, 1, f);
    fwrite(&e->sample_rate, 4, 1, f); fwrite(&byterate, 4, 1, f);
    fwrite(&blockalign, 2, 1, f); fwrite(&e->bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&datalen, 4, 1, f);
    fwrite(e->pcm, 1, datalen, f);
}

int main(int argc, char **argv) {
    const char *dll = NULL, *out = NULL, *tracepath = NULL;
    const char *calls[32], *pokes[16], *dumps[8];
    const char *speak = NULL, *phonemes = NULL, *frames = NULL, *watchspec = NULL;
    const char *hookspec = NULL, *records = NULL, *interp = NULL, *regspec = NULL;
    int level = -1;
    int ncalls = 0, npokes = 0, ndumps = 0, raw = 0, list = 0, verbose = 0;
    unsigned long long limit = 2000000000ULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dll") && i + 1 < argc)        dll = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)   out = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) tracepath = argv[++i];
        else if (!strcmp(argv[i], "--call") && i + 1 < argc && ncalls < 32) calls[ncalls++] = argv[++i];
        else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && npokes < 16) pokes[npokes++] = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc && ndumps < 8) dumps[ndumps++] = argv[++i];
        else if (!strcmp(argv[i], "--speak") && i + 1 < argc)    speak = argv[++i];
        else if (!strcmp(argv[i], "--phonemes") && i + 1 < argc) phonemes = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)   frames = argv[++i];
        else if (!strcmp(argv[i], "--records") && i + 1 < argc)  records = argv[++i];
        else if (!strcmp(argv[i], "--interp") && i + 1 < argc)   interp = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc)    level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc)    watchspec = argv[++i];
        else if (!strcmp(argv[i], "--hook") && i + 1 < argc)     hookspec = argv[++i];
        else if (!strcmp(argv[i], "--regs") && i + 1 < argc)     regspec = argv[++i];
        else if (!strcmp(argv[i], "--raw"))  raw = 1;
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "-v"))     verbose++;
        else if (!strcmp(argv[i], "-vv"))    verbose += 2;
        else { usage(); return 2; }
    }
    if (!dll) { usage(); return 2; }

    emu *e = emu_new();
    if (!e) { fprintf(stderr, "emulator init failed\n"); return 1; }
    e->verbose = verbose;
    e->insn_limit = limit;

    fprintf(stderr, "loading %s\n", dll);
    if (emu_load_pe(e, dll) < 0) { emu_free(e); return 1; }
    fprintf(stderr, "image 0x%08x + 0x%x, entry 0x%08x, %d imports bound\n",
            e->image_base, e->image_size, e->entry, e->nshims);

    if (list) {
        /* Names come back through emu_export, so just probe the table directly. */
        uint32_t pe = emu_rd32(e, e->image_base + 0x3c);
        uint32_t opt = e->image_base + pe + 24;
        uint32_t dir = e->image_base + emu_rd32(e, opt + 96);
        uint32_t nnames = emu_rd32(e, dir + 24);
        uint32_t aname = emu_rd32(e, dir + 32);
        for (uint32_t i = 0; i < nnames; i++) {
            uint32_t nrva = emu_rd32(e, e->image_base + aname + 4 * i);
            char buf[128];
            for (size_t k = 0; k < sizeof buf; k++) {
                uint8_t c = 0;
                uc_mem_read(e->uc, e->image_base + nrva + k, &c, 1);
                buf[k] = (char)c;
                if (!c) break;
            }
            printf("%s 0x%08x\n", buf, emu_export(e, buf));
        }
        emu_free(e);
        return 0;
    }

    if (emu_call_dllmain(e, 1) < 0) {
        fprintf(stderr, "DllMain failed\n");
        emu_free(e);
        return 1;
    }
    fprintf(stderr, "DllMain ok\n");

    if (speak || phonemes || frames || records || interp) {
        const profile *pr = NULL;
        for (int i = 0; i < NPROFILES; i++)
            if (profiles[i].image_size == e->image_size) pr = &profiles[i];
        if (!pr) { fprintf(stderr, "no profile for this build\n"); emu_free(e); return 1; }

        uint32_t fn = emu_export(e, "bstCreateTts");
        uint32_t zero[4] = { 0, 0, 0, 0 }, ret = 0;
        if (!fn || emu_call(e, fn, zero, 4, &ret) < 0 || !ret) {
            fprintf(stderr, "bstCreateTts failed\n");
            emu_free(e);
            return 1;
        }

        const char *text = speak ? speak : (phonemes ? phonemes :
                           (frames ? frames : (records ? records : interp)));
        uint32_t gtext = emu_push_str(e, text);
        /* The engine holds this capacity in a signed 16-bit field and refuses
           to write when it is not greater than the index, so anything above
           32767 disables the output entirely. */
        uint32_t cap = 32000;
        uint32_t gbuf = emu_alloc(e, cap + 16);

        if (tracepath) {
            FILE *tf = fopen(tracepath, "wb");
            if (tf) emu_trace_reads(e, tf);
        }
        if (regspec) emu_hook_regs(e, (uint32_t)strtoul(regspec, NULL, 0), stdout);
        if (hookspec) {
            char hs[256];
            snprintf(hs, sizeof hs, "%s", hookspec);
            char *q = NULL;
            unsigned long pc = strtoul(hs, &q, 0);
            int na = (q && *q == ':') ? atoi(q + 1) : 2;
            emu_hook_call(e, (uint32_t)pc, na, stdout);
        }
        if (watchspec) {
            char ws[256];
            snprintf(ws, sizeof ws, "%s", watchspec);
            char *q = NULL;
            unsigned long lo = strtoul(ws, &q, 0);
            unsigned long len = (q && *q == ':') ? strtoul(q + 1, &q, 0) : 64;
            const char *wf = (q && *q == ':') ? q + 1 : NULL;
            FILE *w = wf ? fopen(wf, "w") : stderr;
            if (w) emu_watch_writes(e, w, (uint32_t)lo, (uint32_t)(lo + len));
        }

        if (records)
            emu_hook_record(e, pr->seg_entry, 0, 8, stdout);

        if (interp) {
            /* Snapshot taken at the frame builder's entry, before it steps the
               parameter state: the target vector, the state, this frame's
               duration and the time left in the transition. */
            const uint32_t a[4] = { pr->targets_cur, pr->state_cur,
                                    pr->frame_dur, pr->trans_left };
            const int      l[4] = { 20, 20, 2, 2 };
            emu_hook_dump(e, pr->build_entry, a, l, 4, 'B', stdout);
            const uint32_t b[1] = { pr->state_cur };
            const int      m[1] = { 20 };
            emu_hook_dump(e, pr->build_done, b, m, 1, 'A', stdout);
        }

        if (phonemes || frames)
            emu_drain_setup(e, pr->phbuf_idx, gbuf, (int)cap - 64, pr->phbuf_after_inc);

        if (phonemes) {
            uint32_t a[6] = { 0, 0, gtext,
                              (uint32_t)(level < 0 ? pr->default_level : level),
                              gbuf, cap };
            fn = emu_export(e, "GetPhBuf");
            if (!fn || emu_call(e, fn, a, 6, &ret) < 0) { emu_free(e); return 1; }
        } else {
            if (frames) {
                /* Point the diagnostic buffer at our allocation and switch the
                   frame dump on. Both are cleared by engine setup, so this has
                   to happen after bstCreateTts and before the speak call. */
                emu_wr32(e, pr->phbuf_ptr, gbuf);
                emu_wr32(e, pr->phbuf_idx, 0);
                uint16_t c = (uint16_t)cap;
                emu_write(e, pr->phbuf_cap, &c, 2);
                uint16_t f = 1;
                emu_write(e, pr->frame_flags, &f, 2);
            }
            uint32_t a[2] = { 0, gtext };
            fn = emu_export(e, "SayBstText");
            if (!fn || emu_call(e, fn, a, 2, &ret) < 0) { emu_free(e); return 1; }
        }

        if (phonemes || frames) {
            emu_drain_flush(e);
            size_t n = 0;
            const char *t = emu_drained(e, &n);
            fwrite(t, 1, n, stdout);
            if (n && t[n - 1] != '\n') putchar('\n');
            fprintf(stderr, "%zu bytes of diagnostic output\n", n);
        }

        if (e->trace) fclose(e->trace);
        fprintf(stderr, "captured %zu bytes, %u Hz, %u ch, %u bit\n",
                e->pcm_len, e->sample_rate, e->channels, e->bits);
        if (out && e->pcm_len) {
            FILE *f = fopen(out, "wb");
            if (f) {
                if (raw) fwrite(e->pcm, 1, e->pcm_len, f);
                else write_wav(f, e);
                fclose(f);
                fprintf(stderr, "wrote %s\n", out);
            }
        }
        emu_free(e);
        return 0;
    }

    if (tracepath) {
        FILE *tf = fopen(tracepath, "wb");
        if (!tf) { fprintf(stderr, "cannot write %s\n", tracepath); emu_free(e); return 1; }
        emu_trace_reads(e, tf);
    }

    for (int c = 0; c < ncalls; c++) {
        /* Applied before every call: engine setup routines clear .bss state,
           so a poke made once up front does not survive them. */
        for (int i = 0; i < npokes; i++) {
            unsigned long addr = 0, val = 0, width = 4;
            char *p = NULL;
            addr = strtoul(pokes[i], &p, 0);
            if (p && *p == '=') val = strtoul(p + 1, &p, 0);
            if (p && *p == ':') width = strtoul(p + 1, NULL, 0);
            if (width > 4) width = 4;
            uint8_t b[4] = { (uint8_t)val, (uint8_t)(val >> 8),
                             (uint8_t)(val >> 16), (uint8_t)(val >> 24) };
            emu_write(e, (uint32_t)addr, b, (uint32_t)width);
        }

        char spec[512];
        snprintf(spec, sizeof spec, "%s", calls[c]);
        char *colon = strchr(spec, ':');
        char *name = spec;
        uint32_t args[16];
        struct outbuf obs[16];
        int argn = 0;
        memset(obs, 0, sizeof obs);

        if (colon) {
            *colon = 0;
            char *p = colon + 1;
            while (p && *p && argn < 16) {
                char *comma = strchr(p, ',');
                /* str: and wstr: payloads may contain no comma; split on the
                   first one only, which is enough for every call we make. */
                if (comma) *comma = 0;
                parse_arg(e, p, &args[argn], &obs[argn]);
                argn++;
                p = comma ? comma + 1 : NULL;
            }
        }

        uint32_t fn = emu_export(e, name);
        if (!fn) { fprintf(stderr, "no export named %s\n", name); emu_free(e); return 1; }

        uint32_t ret = 0;
        fprintf(stderr, "call %s(", name);
        for (int i = 0; i < argn; i++) fprintf(stderr, "%s0x%x", i ? ", " : "", args[i]);
        fprintf(stderr, ")\n");

        int rc = emu_call(e, fn, args, argn, &ret);
        if (rc == -1) { emu_free(e); return 1; }
        if (rc == -2) break;
        fprintf(stderr, "  -> 0x%08x   (%zu bytes captured so far)\n", ret, e->pcm_len);

        for (int i = 0; i < argn; i++) {
            if (!obs[i].addr) continue;
            uint8_t *tmp = malloc(obs[i].len);
            if (tmp && emu_read(e, obs[i].addr, tmp, obs[i].len) == 0) {
                fprintf(stderr, "  arg%d buffer:", i);
                for (uint32_t k = 0; k < obs[i].len && k < 64; k++) fprintf(stderr, " %02x", tmp[k]);
                fprintf(stderr, "\n");
            }
            free(tmp);
        }
    }

    for (int i = 0; i < ndumps; i++) {
        char spec[256];
        snprintf(spec, sizeof spec, "%s", dumps[i]);
        char *p = NULL;
        unsigned long addr = strtoul(spec, &p, 0);
        unsigned long len = (p && *p == ':') ? strtoul(p + 1, &p, 0) : 64;
        const char *file = (p && *p == ':') ? p + 1 : NULL;
        uint8_t *tmp = malloc(len ? len : 1);
        if (!tmp) continue;
        if (emu_read(e, (uint32_t)addr, tmp, (uint32_t)len) == 0) {
            if (file) {
                FILE *df = fopen(file, "wb");
                if (df) { fwrite(tmp, 1, len, df); fclose(df); fprintf(stderr, "dumped 0x%08lx+%lu to %s\n", addr, len, file); }
            } else {
                fwrite(tmp, 1, len, stdout);
            }
        } else fprintf(stderr, "cannot read 0x%08lx+%lu\n", addr, len);
        free(tmp);
    }

    if (e->trace) fclose(e->trace);
    if (verbose) emu_report_shims(e);

    fprintf(stderr, "captured %zu bytes, %u Hz, %u ch, %u bit\n",
            e->pcm_len, e->sample_rate, e->channels, e->bits);

    if (out && e->pcm_len) {
        FILE *f = fopen(out, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", out); emu_free(e); return 1; }
        if (raw) fwrite(e->pcm, 1, e->pcm_len, f);
        else write_wav(f, e);
        fclose(f);
        fprintf(stderr, "wrote %s\n", out);
    }

    emu_free(e);
    return 0;
}
