#include <stdlib.h>
#include <string.h>
#include "neemu.h"

/* The 1998 lattice, run by the original.
 *
 * The other 16-bit oracle stands in for KGMTTS and receives frames from the
 * language module, which is everything above the synthesizer and nothing of
 * it. That left the 1998 audio path never compared against anything, and it
 * was wrong four ways: the excitation and gain tables were read out of the
 * language module rather than the core, the output was treated as sixteen
 * bits when it is eight, the noise generator was the 1995 one, and the voice
 * it starts on was one rather than zero.
 *
 * This drives KNGMM's own lattice instead: frames in, samples out. It needs
 * no Win16 host, because the three entry points it calls touch nothing but
 * the module's own data segment. They are, in the code segment,
 *
 *   0x8fe4  reset: clears the ten lattice states, seeds the noise generator
 *           and points the excitation at the default of eight tables
 *   0x9068  take a frame: unpacks sixteen bytes into the globals
 *   0x91b0  fill: writes samples until the frame's periods are used up
 *
 * The engine hands its host unsigned bytes centred on 0x80. They are widened
 * here to signed sixteen bits the same way the library does, so the two are
 * directly comparable. */

#define KNG_RESET 0x8fe4
#define KNG_FRAME 0x9068
#define KNG_FILL  0x91b0

/* Scratch laid out in one selector: the context the fill entry reads a word
   from, the cursor it advances, the count it decrements, and the buffer. */
#define OFF_CTX   0x00
#define OFF_FRAME 0x40
#define OFF_CUR   0x80
#define OFF_LEFT  0x84
#define OFF_BUF   0x1000
#define BUFSAMP   4096

static void set_ds(nemu *e, uint16_t ds) {
    uint32_t v = ds;
    uc_reg_write(e->uc, UC_X86_REG_DS, &v);
}

static uint8_t *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n ? (size_t)n : 1);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
    fclose(f);
    *len = n;
    return d;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: kngoracle KNGMM.DLL FRAMES OUT.pcm\n");
        return 2;
    }

    long flen = 0;
    uint8_t *fr = slurp(argv[2], &flen);
    if (!fr) { fprintf(stderr, "kng: cannot read %s\n", argv[2]); return 1; }
    long nframes = flen / 16;

    nemu *e = ne_new();
    if (!e) return 1;
    e->insn_limit = 2000000000ULL;
    nemod *m = ne_load(e, argv[1]);
    if (!m || m->nseg < 4) { fprintf(stderr, "kng: cannot load %s\n", argv[1]); return 1; }

    /* The module's own data segment, which every one of the three entries
       addresses through DS. Its imports are never reached, so the unresolved
       ones the loader reports do not matter. */
    uint16_t cs = m->seg[0].sel;
    uint16_t ds = m->seg[m->ds_seg >= 1 && m->ds_seg <= m->nseg ? m->ds_seg - 1 : 3].sel;

    uint16_t scr = ne_alloc_sel(e, 0x10000, 0);
    uint32_t sb = ne_lin(e, scr, 0);
    uint8_t zero[0x100];
    memset(zero, 0, sizeof zero);
    ne_write(e, sb, zero, sizeof zero);
    ne_wr16(e, sb + OFF_CTX + 0x10, 8);   /* selects the fill the engine uses */

    uint32_t dxax = 0;
    set_ds(e, ds);
    if (ne_call(e, cs, KNG_RESET, NULL, 0, &dxax) < 0) {
        fprintf(stderr, "kng: reset failed\n");
        ne_backtrace(e);
        return 1;
    }

    FILE *out = fopen(argv[3], "wb");
    if (!out) { fprintf(stderr, "kng: cannot write %s\n", argv[3]); return 1; }

    long total = 0;
    for (long i = 0; i < nframes; i++) {
        ne_write(e, sb + OFF_FRAME, fr + i * 16, 16);
        uint16_t a1[4] = { 0, scr, OFF_FRAME, scr };
        set_ds(e, ds);
        if (ne_call(e, cs, KNG_FRAME, a1, 4, &dxax) < 0) {
            fprintf(stderr, "kng: frame %ld failed\n", i);
            return 1;
        }
        if ((dxax & 0xffff) == 0) continue;   /* a command, not a frame */

        ne_wr16(e, sb + OFF_CUR, OFF_BUF);
        ne_wr16(e, sb + OFF_CUR + 2, scr);
        ne_wr16(e, sb + OFF_LEFT, BUFSAMP);
        uint16_t a2[6] = { 0, scr, OFF_CUR, scr, OFF_LEFT, scr };
        set_ds(e, ds);
        if (ne_call(e, cs, KNG_FILL, a2, 6, &dxax) < 0) {
            fprintf(stderr, "kng: fill %ld failed\n", i);
            return 1;
        }

        long got = BUFSAMP - ne_rd16(e, sb + OFF_LEFT);
        if (got <= 0 || got > BUFSAMP) continue;
        uint8_t *b = malloc((size_t)got);
        if (!b) return 1;
        ne_read(e, sb + OFF_BUF, b, (uint32_t)got);
        for (long k = 0; k < got; k++) {
            int16_t s = (int16_t)(((int)b[k] - 128) * 256);
            fwrite(&s, 2, 1, out);
        }
        free(b);
        total += got;
    }
    fclose(out);
    fprintf(stderr, "kngoracle: %ld frames, %ld samples\n", nframes, total);
    ne_free(e);
    free(fr);
    return 0;
}
