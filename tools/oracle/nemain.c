#include <stdlib.h>
#include <string.h>
#include "neemu.h"

/* Driver for the 16-bit oracle. Loads the 1998 synthesiser and one language
   module, initialises both, and reports what the module offers. */

static void usage(void) {
    fprintf(stderr,
            "usage: neoracle [-v] [-vv] --core KGMTTS.DLL --lang KGMENG.DLL\n"
            "                [--info] [--dump DIR] [--call NAME[,word...]]\n");
}

int main(int argc, char **argv) {
    const char *core = NULL, *lang = NULL, *call = NULL, *dump = NULL;
    int verbose = 0, info = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-vv")) verbose = 2;
        else if (!strcmp(argv[i], "--info")) info = 1;
        else if (!strcmp(argv[i], "--core") && i + 1 < argc) core = argv[++i];
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) lang = argv[++i];
        else if (!strcmp(argv[i], "--call") && i + 1 < argc) call = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else { usage(); return 2; }
    }
    if (!core) { usage(); return 2; }

    nemu *e = ne_new();
    if (!e) { fprintf(stderr, "cannot start the emulator\n"); return 1; }
    e->verbose = verbose;

    nemod *ml = NULL;
    if (lang) {
        ml = ne_load(e, lang);
        if (!ml) return 1;
    }
    nemod *mc = ne_load(e, core);
    if (!mc) return 1;

    if (info) {
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
    if (dump) {
        for (int i = 0; i < e->nmods; i++) {
            nemod *m = &e->mod[i];
            for (int s = 0; s < m->nseg; s++) {
                char path[512];
                snprintf(path, sizeof path, "%s/%s.%d.bin", dump, m->name, s + 1);
                uint8_t *buf = malloc(m->seg[s].alloc);
                if (!buf) continue;
                ne_read(e, m->seg[s].base, buf, m->seg[s].alloc);
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(buf, 1, m->seg[s].alloc, f); fclose(f); }
                free(buf);
            }
        }
    }

    /* The language module first: the core looks it up while initialising. */
    if (ml) {
        uint16_t ax = 0;
        if (ne_init_module(e, ml, &ax) < 0) {
            fprintf(stderr, "%s entry point faulted\n", ml->name);
            return 1;
        }
        printf("%s init -> %u\n", ml->name, ax);
    }
    {
        uint16_t ax = 0;
        if (ne_init_module(e, mc, &ax) < 0) {
            fprintf(stderr, "%s entry point faulted\n", mc->name);
            return 1;
        }
        printf("%s init -> %u\n", mc->name, ax);
    }

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

    if (verbose) ne_report(e);
    ne_free(e);
    return 0;
}
