#include <stdlib.h>
#include <string.h>
#include "neemu.h"

/* Driver for the 16-bit oracle. Loads the 1998 synthesiser and one language
   module, initialises both, and reports what the module offers. */

static void dump_segments(nemu *e, const char *dir, const char *tag) {
    for (int i = 0; i < e->nmods; i++) {
        nemod *m = &e->mod[i];
        for (int s = 0; s < m->nseg; s++) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s.%d.%s.bin", dir, m->name, s + 1, tag);
            uint8_t *buf = malloc(m->seg[s].alloc);
            if (!buf) continue;
            ne_read(e, m->seg[s].base, buf, m->seg[s].alloc);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(buf, 1, m->seg[s].alloc, f); fclose(f); }
            free(buf);
        }
    }
}

/* ---- driving the language module directly ------------------------------
 *
 * KGMTTS is only a veneer: it hands the language module a table of seven far
 * pointers and calls two entry points. Standing in for it means the oracle
 * feeds the text and receives the frames itself, with no queue, no window and
 * no wave device in the way. */

static const char *g_text;
static int   g_pos;
static int   g_trace;
static int   g_eof = 0;

static void frames_append(nemu *e, const uint8_t *f, size_t n) {
    if (e->frames_len + n > e->frames_cap) {
        size_t cap = e->frames_cap ? e->frames_cap * 2 : 4096;
        while (cap < e->frames_len + n) cap *= 2;
        uint8_t *q = realloc(e->frames, cap);
        if (!q) return;
        e->frames = q;
        e->frames_cap = cap;
    }
    memcpy(e->frames + e->frames_len, f, n);
    e->frames_len += n;
}

static void cb_getc(nemu *e, uint32_t sp) {
    (void)sp;
    int c = g_text[g_pos] ? (unsigned char)g_text[g_pos++] : g_eof;
    e->ax = (uint16_t)c;
    if (g_trace) fprintf(stderr, "  getc -> %d\n", c);
}

static void cb_putfr(nemu *e, uint32_t sp) {
    uint32_t p = ne_lin(e, ne_argc(e, sp, 1), ne_argc(e, sp, 0));
    uint8_t f[16];
    if (ne_read(e, p, f, sizeof f) < 0) return;
    frames_append(e, f, sizeof f);
    e->ax = 0;
}

static void cb_void(nemu *e, uint32_t sp) { (void)e; (void)sp; }

static void cb_word(nemu *e, uint32_t sp) {
    if (g_trace) fprintf(stderr, "  service(%d)\n", (int16_t)ne_argc(e, sp, 0));
    (void)e;
}

static int drive(nemu *e, nemod *m, const char *text) {
    g_text = text;
    g_pos = 0;

    uint16_t svc = ne_alloc_sel(e, 64, 0);
    uint32_t slots[7] = {
        ne_callback(e, cb_void,  "svc+00"),
        ne_callback(e, cb_void,  "svc+04"),
        ne_callback(e, cb_void,  "svc+08"),
        ne_callback(e, cb_word,  "svc+0c"),
        ne_callback(e, cb_word,  "svc+10"),
        ne_callback(e, cb_putfr, "putfr"),
        ne_callback(e, cb_getc,  "getc"),
    };
    for (int i = 0; i < 7; i++) {
        ne_wr16(e, ne_lin(e, svc, 0) + i * 4, (uint16_t)slots[i]);
        ne_wr16(e, ne_lin(e, svc, 0) + i * 4 + 2, (uint16_t)(slots[i] >> 16));
    }

    uint16_t seg = 0, off = 0;
    if (ne_export(e, m, "INITLANGDLL", &seg, &off) < 0) {
        fprintf(stderr, "%s has no INITLANGDLL\n", m->name);
        return -1;
    }
    uint16_t args[2] = { 0, svc };
    uint32_t tab = 0;
    if (ne_call(e, seg, off, args, 2, &tab) < 0) return -1;
    uint32_t tl = ne_lin(e, (uint16_t)(tab >> 16), (uint16_t)tab);
    uint8_t ver = 0;
    ne_read(e, tl, &ver, 1);
    printf("language table at %04x:%04x, version %u\n",
           (uint16_t)(tab >> 16), (uint16_t)tab, ver);

    /* The three data pointers before the two entry points: the parameter
       blocks the host reads and writes. */
    for (int k = 0; k < 3; k++) {
        uint16_t off = ne_rd16(e, tl + 5 + k * 4), sg = ne_rd16(e, tl + 7 + k * 4);
        uint32_t at = ne_lin(e, sg, off);
        printf("  block %d at %04x:%04x", k, sg, off);
        for (int i = 0; i < 32; i++)
            printf(" %02x", (unsigned)(at ? ne_rd16(e, at + i) & 0xff : 0));
        printf("\n");
    }
    printf("  table bytes:");
    for (int i = 0; i < 5; i++) {
        uint8_t v = 0;
        ne_read(e, tl + i, &v, 1);
        printf(" %02x", v);
    }
    printf("\n");

    uint16_t a_off = ne_rd16(e, tl + 0x11), a_seg = ne_rd16(e, tl + 0x13);
    uint16_t b_off = ne_rd16(e, tl + 0x15), b_seg = ne_rd16(e, tl + 0x17);
    printf("  reset %04x:%04x  run %04x:%04x\n", a_seg, a_off, b_seg, b_off);

    uint32_t r = 0;
    if (ne_call(e, a_seg, a_off, NULL, 0, &r) < 0) return -1;
    int rc = ne_call(e, b_seg, b_off, NULL, 0, &r);
    printf("run -> %08x%s, %zu frame bytes, %d characters taken\n",
           r, rc < 0 ? " (stopped early)" : "", e->frames_len, g_pos);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "usage: neoracle [-v] [-vv] --core KGMTTS.DLL --lang KGMENG.DLL\n"
            "                [--info] [--dump DIR] [--call NAME[,word...]]\n"
            "                [--engine TEXT] [--frames FILE] [--trace FILE]\n");
}

int main(int argc, char **argv) {
    const char *core = NULL, *lang = NULL, *call = NULL, *dump = NULL;
    const char *say = NULL, *engine = NULL, *probe = NULL, *tracepath = NULL;
    const char *peek = NULL;
    const char *wframes = NULL;
    int verbose = 0, info = 0, btrace = 0;
    unsigned long long limit = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-vv")) verbose = 2;
        else if (!strcmp(argv[i], "--info")) info = 1;
        else if (!strcmp(argv[i], "--btrace")) btrace = 1;
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) probe = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) tracepath = argv[++i];
        else if (!strcmp(argv[i], "--peek") && i + 1 < argc) peek = argv[++i];
        else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--eof") && i + 1 < argc) g_eof = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--core") && i + 1 < argc) core = argv[++i];
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) lang = argv[++i];
        else if (!strcmp(argv[i], "--call") && i + 1 < argc) call = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--say") && i + 1 < argc) say = argv[++i];
        else if (!strcmp(argv[i], "--engine") && i + 1 < argc) engine = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) wframes = argv[++i];
        else { usage(); return 2; }
    }
    if (!core && !lang) { usage(); return 2; }

    nemu *e = ne_new();
    if (!e) { fprintf(stderr, "cannot start the emulator\n"); return 1; }
    e->verbose = verbose;
    if (btrace) ne_backtrace(e);
    e->insn_limit = limit;

    g_trace = verbose > 1;
    nemod *ml = NULL;
    if (lang) {
        ml = ne_load(e, lang);
        if (!ml) return 1;
        if (!core && ml->ds_seg >= 1 && ml->ds_seg <= ml->nseg)
            ne_set_stack(e, ml->seg[ml->ds_seg - 1].sel, 0xf000);
    }
    nemod *mc = NULL;
    if (core) {
        mc = ne_load(e, core);
        if (!mc) return 1;
    }

    if (info) {
        printf("thunks at selector %04x\n", e->thunk_sel);
        for (int i = 0; i < e->nthunk; i++)
            printf("  %04x:%04x %s\n", e->thunk_sel, i * 4, e->thunk[i].name);
        for (int i = 0; i < e->nmods; i++) {
            nemod *m = &e->mod[i];
            printf("%s handle %04x, %d segments\n", m->name, m->handle, m->nseg);
            for (int s = 0; s < m->nseg; s++)
                printf("  seg %2d sel %04x lin %08x len %5u alloc %5u %s\n",
                       s + 1, m->seg[s].sel, m->seg[s].base, m->seg[s].len,
                       m->seg[s].alloc, (m->seg[s].flags & 1) ? "data" : "code");
            for (int k = 0; k < m->nexp; k++) {
                uint16_t sg = 0, of = 0;
                if (ne_export(e, m, m->exp[k].name, &sg, &of) == 0)
                    printf("  export %-24s @%-3d %04x:%04x\n",
                           m->exp[k].name, m->exp[k].ord, sg, of);
            }
        }
    }

    /* Relocated segment images, which is the only form the code can be read
       in: in the file every fixup site holds a chain link instead. */
    if (dump) dump_segments(e, dump, "pre");

    /* The language module first: the core looks it up while initialising. */
    if (ml) {
        uint16_t ax = 0;
        if (ne_init_module(e, ml, &ax) < 0) {
            fprintf(stderr, "%s entry point faulted\n", ml->name);
            return 1;
        }
        printf("%s init -> %u\n", ml->name, ax);
    }
    if (mc) {
        uint16_t ax = 0;
        if (ne_init_module(e, mc, &ax) < 0) {
            fprintf(stderr, "%s entry point faulted\n", mc->name);
            return 1;
        }
        printf("%s init -> %u\n", mc->name, ax);
    }

    FILE *tracef = NULL;
    if (tracepath) {
        tracef = fopen(tracepath, "w");
        if (tracef) ne_trace_reads(e, tracef);
    }

    if (engine) {
        if (!ml) { fprintf(stderr, "--engine needs --lang\n"); return 1; }
        if (peek) {
            /* seg:off:dataseg:addr,addr,... */
            char buf[256];
            snprintf(buf, sizeof buf, "%s", peek);
            unsigned seg = 1, off = 0, dseg = 16;
            char *rest = strchr(buf, ':');
            if (rest) {
                sscanf(buf, "%u:%x:%u", &seg, &off, &dseg);
                char *list = strrchr(buf, ':');
                uint16_t at[8];
                int na = 0;
                for (char *t = strtok(list + 1, ","); t && na < 8; t = strtok(NULL, ","))
                    at[na++] = (uint16_t)strtoul(t, NULL, 16);
                if (seg >= 1 && seg <= (unsigned)ml->nseg &&
                    dseg >= 1 && dseg <= (unsigned)ml->nseg)
                    ne_hook_peek(e, ml->seg[seg - 1].sel, (uint16_t)off,
                                 ml->seg[dseg - 1].sel, at, na);
            }
        }
        if (probe) {
            char buf[256];
            snprintf(buf, sizeof buf, "%s", probe);
            for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
                unsigned seg = 1, off = 0, n = 8;
                if (sscanf(t, "%u:%x:%u", &seg, &off, &n) < 2) continue;
                if (seg < 1 || seg > (unsigned)ml->nseg) continue;
                ne_hook_regs(e, ml->seg[seg - 1].sel, (uint16_t)off, (int)n);
            }
        }
        if (drive(e, ml, engine) < 0) { fprintf(stderr, "the engine faulted\n"); return 1; }
        if (wframes) {
            FILE *f = fopen(wframes, "wb");
            if (f) { fwrite(e->frames, 1, e->frames_len, f); fclose(f); }
        }
    }

    if (dump) dump_segments(e, dump, "post");

    if (call) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", call);
        char *save = NULL;
        char *name = strtok_r(buf, ",", &save);
        uint16_t args[16];
        int n = 0;
        for (char *t; (t = strtok_r(NULL, ",", &save)) && n < 16;)
            args[n++] = (uint16_t)strtoul(t, NULL, 0);
        uint16_t seg = 0, off = 0;
        nemod *m = NULL;
        for (int i = 0; i < e->nmods && !m; i++)
            if (ne_export(e, &e->mod[i], name, &seg, &off) == 0) m = &e->mod[i];
        if (!m) { fprintf(stderr, "no export named %s\n", name); return 1; }
        uint32_t r = 0;
        if (ne_call(e, seg, off, args, n, &r) < 0) {
            fprintf(stderr, "%s faulted\n", name);
            return 1;
        }
        printf("%s -> %08x\n", name, r);
    }

    if (say) {
        uint16_t sel = ne_global_alloc(e, 4096);
        ne_write(e, ne_lin(e, sel, 0), say, (uint32_t)strlen(say) + 1);
        uint16_t seg = 0, off = 0;
        if (ne_export(e, mc, "TTSCONTROL", &seg, &off) < 0) {
            fprintf(stderr, "no TTSCONTROL\n");
            return 1;
        }
        uint16_t args[3] = { 0, 0, sel };
        uint32_t r = 0;
        if (ne_call(e, seg, off, args, 3, &r) < 0) {
            fprintf(stderr, "TTSCONTROL(0) faulted\n");
            return 1;
        }
        printf("TTSCONTROL(0) -> %08x, %zu frame bytes\n", r, e->frames_len);
    }

    if (tracef) { ne_trace_report(e); fclose(tracef); }
    if (verbose) ne_report(e);
    ne_free(e);
    return 0;
}
