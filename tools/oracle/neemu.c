#include <stdlib.h>
#include <string.h>
#include "neemu.h"

/* ---- descriptors -------------------------------------------------------- */

static void set_desc(uint8_t *d, uint32_t base, uint32_t limit,
                     uint8_t access, uint8_t gran) {
    if (limit > 0xfffff) { limit >>= 12; gran |= 0x8; }
    d[0] = limit & 0xff;
    d[1] = (limit >> 8) & 0xff;
    d[2] = base & 0xff;
    d[3] = (base >> 8) & 0xff;
    d[4] = (base >> 16) & 0xff;
    d[5] = access;
    d[6] = ((limit >> 16) & 0x0f) | ((gran & 0x0f) << 4);
    d[7] = (base >> 24) & 0xff;
}

uint16_t ne_alloc_sel(nemu *e, uint32_t bytes, int code) {
    if (!bytes) bytes = 1;
    uint32_t windows = (bytes + NE_WINDOW - 1) / NE_WINDOW;
    int first = -1, run = 0;
    for (int i = 1; i < NE_MAX_SEL; i++) {
        if (e->sel[i].used) { run = 0; first = -1; continue; }
        if (first < 0) first = i;
        if ((uint32_t)++run >= windows) break;
    }
    if (first < 0 || (uint32_t)run < windows) return 0;
    if (e->arena_next + windows * NE_WINDOW > NE_ARENA_BASE + NE_ARENA_SIZE) return 0;

    uint8_t d[8];
    for (uint32_t k = 0; k < windows; k++) {
        /* One descriptor per 64K window, laid out so that adding __AHINCR to a
           selector steps to the next window of the same block, which is how
           16-bit code walks anything larger than a segment. */
        uint32_t base = e->arena_next + k * NE_WINDOW;
        uint32_t left = bytes - k * NE_WINDOW;
        uint32_t limit = left > NE_WINDOW ? NE_WINDOW - 1 : left - 1;
        e->sel[first + k].used = 1;
        e->sel[first + k].base = base;
        e->sel[first + k].limit = limit;
        set_desc(d, base, limit, code ? 0x9a : 0x92, 0x0);
        uc_mem_write(e->uc, NE_GDT_BASE + (first + k) * 8, d, 8);
    }
    e->arena_next += windows * NE_WINDOW;
    return (uint16_t)(first * 8);
}

uint32_t ne_lin(nemu *e, uint16_t sel, uint16_t off) {
    int i = sel >> 3;
    if (i <= 0 || i >= NE_MAX_SEL || !e->sel[i].used) return 0;
    return e->sel[i].base + off;
}

/* ---- memory helpers ----------------------------------------------------- */

int ne_read(nemu *e, uint32_t lin, void *dst, uint32_t n) {
    return uc_mem_read(e->uc, lin, dst, n) == UC_ERR_OK ? 0 : -1;
}
int ne_write(nemu *e, uint32_t lin, const void *src, uint32_t n) {
    return uc_mem_write(e->uc, lin, src, n) == UC_ERR_OK ? 0 : -1;
}
uint16_t ne_rd16(nemu *e, uint32_t lin) {
    uint16_t v = 0;
    uc_mem_read(e->uc, lin, &v, 2);
    return v;
}
void ne_wr16(nemu *e, uint32_t lin, uint16_t v) {
    uc_mem_write(e->uc, lin, &v, 2);
}

int ne_str(nemu *e, uint32_t lin, char *dst, int cap) {
    int i = 0;
    for (; i < cap - 1; i++) {
        uint8_t c = 0;
        if (uc_mem_read(e->uc, lin + i, &c, 1) != UC_ERR_OK) break;
        dst[i] = (char)c;
        if (!c) return i;
    }
    dst[i] = 0;
    return i;
}

/* Pascal pushes left to right, so the first argument ends up highest. sp is
   the address of the word just above the far return address. */
uint16_t ne_argw(nemu *e, uint32_t sp, int nwords, int i) {
    return ne_rd16(e, sp + (uint32_t)(nwords - 1 - i) * 2);
}
uint32_t ne_argd(nemu *e, uint32_t sp, int nwords, int i) {
    uint32_t lo = ne_rd16(e, sp + (uint32_t)(nwords - 2 - i) * 2);
    uint32_t hi = ne_rd16(e, sp + (uint32_t)(nwords - 1 - i) * 2);
    return lo | (hi << 16);
}

uint16_t ne_global_alloc(nemu *e, uint32_t bytes) {
    if (e->ngmem >= (int)(sizeof e->gmem / sizeof e->gmem[0])) return 0;
    uint16_t sel = ne_alloc_sel(e, bytes, 0);
    if (!sel) return 0;
    static const uint8_t zeros[4096] = { 0 };
    uint32_t base = ne_lin(e, sel, 0);
    for (uint32_t off = 0; off < bytes; off += sizeof zeros) {
        uint32_t chunk = bytes - off < sizeof zeros ? bytes - off : sizeof zeros;
        uc_mem_write(e->uc, base + off, zeros, chunk);
    }
    e->gmem[e->ngmem].sel = sel;
    e->gmem[e->ngmem].size = bytes;
    e->gmem[e->ngmem].used = 1;
    e->ngmem++;
    return sel;
}

/* ---- hooks -------------------------------------------------------------- */

static void hook_thunk(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)size;
    nemu *e = ud;
    int idx = (int)((addr - e->thunk_base) / 4);
    if (idx < 0 || idx >= e->nthunk || !e->thunk[idx]) return;
    const ne_import *imp = e->thunk[idx];
    e->ncalled[idx]++;

    uint32_t ss = 0, sp = 0;
    uc_reg_read(e->uc, UC_X86_REG_SS, &ss);
    uc_reg_read(e->uc, UC_X86_REG_SP, &sp);
    uint32_t frame = ne_lin(e, (uint16_t)ss, (uint16_t)(sp + 4));

    e->ax = e->dx = 0;
    if (e->verbose > 1)
        fprintf(stderr, "  [ne] %s.%s\n", imp->mod, imp->name);
    if (imp->fn) imp->fn(e, frame);
    else if (e->verbose)
        fprintf(stderr, "  [ne] %s.%s unimplemented, returning zero\n",
                imp->mod, imp->name);

    uint32_t ax = e->ax, dx = e->dx;
    uc_reg_write(e->uc, UC_X86_REG_AX, &ax);
    uc_reg_write(e->uc, UC_X86_REG_DX, &dx);
    /* The `retf imm16` planted in this slot performs the pascal cleanup. */
}

static void hook_trap(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    nemu *e = ud;
    e->stopped = 1;
    uc_emu_stop(e->uc);
}

static bool hook_invalid(uc_engine *uc, uc_mem_type t, uint64_t addr,
                         int size, int64_t val, void *ud) {
    (void)uc; (void)size; (void)val;
    nemu *e = ud;
    uint32_t cs = 0, ip = 0, ss = 0, sp = 0;
    uc_reg_read(e->uc, UC_X86_REG_CS, &cs);
    uc_reg_read(e->uc, UC_X86_REG_IP, &ip);
    uc_reg_read(e->uc, UC_X86_REG_SS, &ss);
    uc_reg_read(e->uc, UC_X86_REG_SP, &sp);
    fprintf(stderr, "ne: unmapped %s at 0x%08llx from %04x:%04x (ss:sp %04x:%04x)\n",
            t == UC_MEM_READ_UNMAPPED ? "read" :
            t == UC_MEM_WRITE_UNMAPPED ? "write" : "fetch",
            (unsigned long long)addr, cs, ip, ss, sp);
    e->faulted = 1;
    return false;
}

/* ---- construction ------------------------------------------------------- */

nemu *ne_new(void) {
    nemu *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &e->uc) != UC_ERR_OK) { free(e); return NULL; }

    uc_mem_map(e->uc, NE_ARENA_BASE, NE_ARENA_SIZE, UC_PROT_ALL);
    uc_mem_map(e->uc, NE_GDT_BASE, 0x1000, UC_PROT_READ | UC_PROT_WRITE);
    e->arena_next = NE_ARENA_BASE;

    static const uint8_t zero[8] = { 0 };
    uc_mem_write(e->uc, NE_GDT_BASE, zero, 8);
    uc_x86_mmr gdtr = { 0, NE_GDT_BASE, NE_MAX_SEL * 8 - 1, 0 };
    uc_reg_write(e->uc, UC_X86_REG_GDTR, &gdtr);

    e->stack_sel = ne_alloc_sel(e, NE_WINDOW, 0);
    e->stack_base = ne_lin(e, e->stack_sel, 0);
    e->trap_sel = ne_alloc_sel(e, 0x100, 1);
    e->trap_base = ne_lin(e, e->trap_sel, 0);
    e->thunk_sel = ne_alloc_sel(e, NE_MAX_THUNK * 4, 1);
    e->thunk_base = ne_lin(e, e->thunk_sel, 0);

    uint8_t page[0x100];
    memset(page, 0xcb, sizeof page);          /* retf, so a stray trap returns */
    uc_mem_write(e->uc, e->trap_base, page, sizeof page);

    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_trap, e,
                e->trap_base, e->trap_base + 0xff);
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_thunk, e,
                e->thunk_base, e->thunk_base + NE_MAX_THUNK * 4 - 1);
    uc_hook_add(e->uc, &h, UC_HOOK_MEM_INVALID, hook_invalid, e, 1, 0);
    return e;
}

void ne_free(nemu *e) {
    if (!e) return;
    for (int i = 0; i < e->nmods; i++) free(e->mod[i].file);
    if (e->uc) uc_close(e->uc);
    free(e->frames);
    free(e);
}

void ne_frames_reset(nemu *e) { e->frames_len = 0; }

/* ---- NE loading --------------------------------------------------------- */

static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t g32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int thunk_for(nemu *e, const ne_import *imp, uint16_t *sel, uint16_t *off) {
    for (int i = 0; i < e->nthunk; i++)
        if (e->thunk[i] == imp) { *sel = e->thunk_sel; *off = (uint16_t)(i * 4); return 0; }
    if (e->nthunk >= NE_MAX_THUNK) return -1;
    int i = e->nthunk++;
    e->thunk[i] = imp;
    uint8_t slot[4] = { 0xca, 0, 0, 0x90 };   /* retf imm16 */
    uint16_t pop = (uint16_t)(imp->words * 2);
    slot[1] = pop & 0xff;
    slot[2] = pop >> 8;
    uc_mem_write(e->uc, e->thunk_base + i * 4, slot, 4);
    *sel = e->thunk_sel;
    *off = (uint16_t)(i * 4);
    return 0;
}

static void read_names(nemod *m, const uint8_t *d, size_t flen, uint32_t at,
                       uint32_t limit, int skip_first) {
    uint32_t p = at;
    int first = 1;
    while (p < flen && (!limit || p < at + limit) && d[p]) {
        int n = d[p];
        if (p + 1 + n + 2 > flen) break;
        if (!(first && skip_first) && m->nexp < (int)(sizeof m->exp / sizeof m->exp[0])) {
            int k = n < (int)sizeof m->exp[0].name - 1 ? n : (int)sizeof m->exp[0].name - 1;
            memcpy(m->exp[m->nexp].name, d + p + 1, k);
            m->exp[m->nexp].name[k] = 0;
            m->exp[m->nexp].ord = g16(d + p + 1 + n);
            m->nexp++;
        }
        first = 0;
        p += 1 + n + 2;
    }
}

nemod *ne_load(nemu *e, const char *path) {
    if (e->nmods >= NE_MAX_MODS) return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "ne: cannot open %s\n", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    long flen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc(flen);
    if (!d || fread(d, 1, flen, fp) != (size_t)flen) {
        fprintf(stderr, "ne: read failed\n"); fclose(fp); free(d); return NULL;
    }
    fclose(fp);

    uint32_t ne = g32(d + 0x3c);
    if (ne + 0x40 > (uint32_t)flen || d[ne] != 'N' || d[ne + 1] != 'E') {
        fprintf(stderr, "ne: %s is not an NE module\n", path);
        free(d);
        return NULL;
    }

    nemod *m = &e->mod[e->nmods];
    memset(m, 0, sizeof *m);
    m->file = d;
    m->flen = flen;
    m->ne = ne;
    m->handle = (uint16_t)(0x1000 + e->nmods * 0x10);

    const uint8_t *h = d + ne;
    uint32_t ent_off = g16(h + 0x04), ent_len = g16(h + 0x06);
    m->ds_seg = g16(h + 0x0e);
    m->entry_ip = (uint16_t)g16(h + 0x14);
    m->entry_cs = (uint16_t)g16(h + 0x16);
    int nseg = g16(h + 0x1c), nmod = g16(h + 0x1e);
    uint32_t seg_off = g16(h + 0x22), res_off = g16(h + 0x26);
    uint32_t mod_off = g16(h + 0x28), imp_off = g16(h + 0x2a);
    uint32_t nonres_off = g32(h + 0x2c), nonres_len = g16(h + 0x30);
    int shift = g16(h + 0x32);
    if (!shift) shift = 9;

    if (nseg > NE_MAX_SEG || nmod > NE_MAX_MOD) {
        fprintf(stderr, "ne: %s has too many segments or modules\n", path);
        free(d);
        return NULL;
    }
    m->nseg = nseg;
    m->nmod = nmod;
    for (int i = 0; i < nmod; i++) {
        uint32_t noff = g16(d + ne + mod_off + i * 2);
        uint32_t p = ne + imp_off + noff;
        int n = d[p];
        if (n > (int)sizeof m->imports[0] - 1) n = (int)sizeof m->imports[0] - 1;
        memcpy(m->imports[i], d + p + 1, n);
        m->imports[i][n] = 0;
    }

    read_names(m, d, flen, ne + res_off, 0, 1);
    if (nonres_len) read_names(m, d, flen, nonres_off, nonres_len, 0);
    {
        int nlen = d[ne + res_off];
        if (nlen > (int)sizeof m->name - 1) nlen = (int)sizeof m->name - 1;
        memcpy(m->name, d + ne + res_off + 1, nlen);
        m->name[nlen] = 0;
    }

    /* The entry table, needed both for exports and for movable-segment
       relocations, which name an ordinal rather than a segment. */
    {
        uint32_t p = ne + ent_off;
        int ordinal = 1;
        while (p + 2 <= ne + ent_off + ent_len && d[p]) {
            int cnt = d[p], ind = d[p + 1];
            p += 2;
            if (ind == 0) { ordinal += cnt; continue; }
            for (int k = 0; k < cnt && ordinal < (int)(sizeof m->ent / sizeof m->ent[0]); k++) {
                if (ind == 0xff) {
                    m->ent[ordinal].seg = d[p + 3];
                    m->ent[ordinal].off = g16(d + p + 4);
                    p += 6;
                } else {
                    m->ent[ordinal].seg = (uint8_t)ind;
                    m->ent[ordinal].off = g16(d + p + 1);
                    p += 3;
                }
                if (ordinal > m->nent) m->nent = ordinal;
                ordinal++;
            }
        }
    }

    /* Map the segments before relocating, because a relocation in one may name
       the selector of another. */
    for (int i = 0; i < nseg; i++) {
        const uint8_t *s = d + ne + seg_off + i * 8;
        uint32_t sector = g16(s), len = g16(s + 2);
        uint32_t sflags = g16(s + 4), alloc = g16(s + 6);
        if (!len) len = 0x10000;
        if (!alloc) alloc = 0x10000;
        if (alloc < len) alloc = len;
        uint16_t sel = ne_alloc_sel(e, alloc, (sflags & 1) ? 0 : 1);
        if (!sel) { fprintf(stderr, "ne: out of selectors\n"); return NULL; }
        m->seg[i].sel = sel;
        m->seg[i].base = ne_lin(e, sel, 0);
        m->seg[i].len = len;
        m->seg[i].alloc = alloc;
        m->seg[i].flags = sflags;

        uint8_t *zeros = calloc(1, alloc);
        if (zeros) { uc_mem_write(e->uc, m->seg[i].base, zeros, alloc); free(zeros); }
        if (sector) {
            uint32_t at = sector << shift;
            uint32_t n = len;
            if (at + n > (uint32_t)flen) n = (uint32_t)flen - at;
            uc_mem_write(e->uc, m->seg[i].base, d + at, n);
        }
        /* A data segment must also be executable when the module makes a code
           alias of it, and Unicorn's protection is per page rather than per
           descriptor, so nothing is gained by being strict. */
    }

    /* Relocations. Each record heads a chain: the word at the target holds the
       offset of the next place to patch, ending at 0xFFFF. */
    int unresolved = 0;
    for (int i = 0; i < nseg; i++) {
        const uint8_t *s = d + ne + seg_off + i * 8;
        uint32_t sector = g16(s), len = g16(s + 2), sflags = g16(s + 4);
        if (!len) len = 0x10000;
        if (!(sflags & 0x100) || !sector) continue;
        uint32_t p = (sector << shift) + len;
        if (p + 2 > (uint32_t)flen) continue;
        int nrel = g16(d + p);
        for (int k = 0; k < nrel; k++) {
            const uint8_t *r = d + p + 2 + k * 8;
            if (p + 2 + (uint32_t)(k + 1) * 8 > (uint32_t)flen) break;
            int at = r[0] & 0x0f, rt = r[1];
            uint32_t off = g16(r + 2), a = g16(r + 4), b = g16(r + 6);
            uint32_t value = 0;
            int isconst = 0, ok = 1;

            switch (rt & 3) {
            case 0: {                       /* internal reference */
                int sn = (int)a;
                uint32_t o = b;
                if (sn == 0xff) {
                    int ord = (int)b;
                    if (ord <= 0 || ord >= (int)(sizeof m->ent / sizeof m->ent[0]) ||
                        !m->ent[ord].seg) { ok = 0; break; }
                    sn = m->ent[ord].seg;
                    o = m->ent[ord].off;
                }
                if (sn < 1 || sn > nseg) { ok = 0; break; }
                value = ((uint32_t)m->seg[sn - 1].sel << 16) | (o & 0xffff);
                break;
            }
            case 1: case 2: {               /* imported ordinal or name */
                const char *mod = (a >= 1 && a <= (uint32_t)nmod) ? m->imports[a - 1] : "?";
                char nbuf[64];
                nbuf[0] = 0;
                int ordinal = -1;
                if ((rt & 3) == 1) ordinal = (int)b;
                else {
                    uint32_t q = ne + imp_off + b;
                    int n = d[q];
                    if (n > (int)sizeof nbuf - 1) n = (int)sizeof nbuf - 1;
                    memcpy(nbuf, d + q + 1, n);
                    nbuf[n] = 0;
                }
                const ne_import *imp = ne_import_find(mod, ordinal, nbuf[0] ? nbuf : NULL);
                if (!imp) {
                    unresolved++;
                    if (e->verbose)
                        fprintf(stderr, "ne: %s imports %s.%s, unresolved\n",
                                m->name, mod, nbuf[0] ? nbuf : "(ordinal)");
                    ok = 0;
                    break;
                }
                if (imp->isconst) { value = imp->value; isconst = 1; }
                else {
                    uint16_t tsel, toff;
                    if (thunk_for(e, imp, &tsel, &toff) < 0) { ok = 0; break; }
                    value = ((uint32_t)tsel << 16) | toff;
                }
                break;
            }
            default:                        /* an OS fixup, which we do not need */
                ok = 0;
                break;
            }
            if (!ok) continue;

            uint32_t base = m->seg[i].base;
            uint32_t cur = off;
            for (int guard = 0; guard < 4096; guard++) {
                if (cur == 0xffff || cur + 2 > m->seg[i].alloc) break;
                uint16_t next = ne_rd16(e, base + cur);
                switch (at) {
                case 0:                                   /* low byte */
                    { uint8_t v = (uint8_t)(value & 0xff);
                      uc_mem_write(e->uc, base + cur, &v, 1); }
                    break;
                case 2:                                   /* selector only */
                    ne_wr16(e, base + cur, isconst ? (uint16_t)value
                                                   : (uint16_t)(value >> 16));
                    break;
                case 3:                                   /* seg:off */
                    ne_wr16(e, base + cur, (uint16_t)value);
                    if (cur + 4 <= m->seg[i].alloc)
                        ne_wr16(e, base + cur + 2, isconst ? (uint16_t)(value >> 16)
                                                           : (uint16_t)(value >> 16));
                    break;
                case 5:                                   /* offset only */
                    ne_wr16(e, base + cur, (uint16_t)value);
                    break;
                default:
                    ne_wr16(e, base + cur, (uint16_t)value);
                    break;
                }
                if (rt & 4) break;          /* additive: no chain */
                if (at == 0) break;         /* a byte fixup cannot chain */
                cur = next;
            }
        }
    }
    if (unresolved)
        fprintf(stderr, "ne: %s has %d unresolved import(s)\n", m->name, unresolved);

    e->nmods++;
    if (e->verbose)
        fprintf(stderr, "ne: loaded %s, %d segments, entry %d:%04x, ds seg %d\n",
                m->name, nseg, m->entry_cs, m->entry_ip, m->ds_seg);
    return m;
}

int ne_export(nemu *e, nemod *m, const char *name, uint16_t *seg, uint16_t *off) {
    (void)e;
    /* The C compiler that built these modules prefixes an underscore, so a
       caller naming the source symbol still finds it. */
    char alt[64];
    snprintf(alt, sizeof alt, "_%s", name);
    for (int i = 0; i < m->nexp; i++) {
        if (strcmp(m->exp[i].name, name) != 0 &&
            strcmp(m->exp[i].name, alt) != 0) continue;
        int o = m->exp[i].ord;
        if (o <= 0 || o > m->nent || !m->ent[o].seg) return -1;
        int sn = m->ent[o].seg;
        if (sn < 1 || sn > m->nseg) return -1;
        *seg = m->seg[sn - 1].sel;
        *off = m->ent[o].off;
        return 0;
    }
    return -1;
}

/* ---- running ------------------------------------------------------------ */

static int run(nemu *e) {
    e->stopped = 0;
    e->faulted = 0;
    uint32_t cs = 0, ip = 0;
    for (long turn = 0;; turn++) {
        uc_reg_read(e->uc, UC_X86_REG_CS, &cs);
        uc_reg_read(e->uc, UC_X86_REG_IP, &ip);
        uc_err err = uc_emu_start(e->uc, ip, 0xfffffff0u, 0, e->insn_limit);
        if (err != UC_ERR_OK) {
            uc_reg_read(e->uc, UC_X86_REG_CS, &cs);
            uc_reg_read(e->uc, UC_X86_REG_IP, &ip);
            fprintf(stderr, "ne: %s at %04x:%04x\n", uc_strerror(err), cs, ip);
            return -1;
        }
        if (e->stopped) return 0;
        if (e->faulted) return -1;
        if (e->insn_limit) {
            fprintf(stderr, "ne: instruction budget exhausted\n");
            return -2;
        }
        if (turn > 64) { fprintf(stderr, "ne: stalled\n"); return -2; }
    }
}

int ne_call(nemu *e, uint16_t seg, uint16_t off,
            const uint16_t *args, int nwords, uint32_t *dxax) {
    uint32_t sp = 0xf000;
    uint32_t ss = e->stack_sel;
    /* The engine's own exports are far cdecl, so the first argument has to end
       up lowest: push the list backwards. */
    for (int i = nwords - 1; i >= 0; i--) {
        sp -= 2;
        ne_wr16(e, e->stack_base + sp, args[i]);
    }
    sp -= 2; ne_wr16(e, e->stack_base + sp, e->trap_sel);
    sp -= 2; ne_wr16(e, e->stack_base + sp, 0);

    uint32_t cs = seg, ip = off, bp = 0;
    uc_reg_write(e->uc, UC_X86_REG_SS, &ss);
    uc_reg_write(e->uc, UC_X86_REG_ESP, &sp);
    uc_reg_write(e->uc, UC_X86_REG_EBP, &bp);
    uc_reg_write(e->uc, UC_X86_REG_CS, &cs);
    uc_reg_write(e->uc, UC_X86_REG_IP, &ip);

    if (run(e) < 0) return -1;
    if (dxax) {
        uint32_t ax = 0, dx = 0;
        uc_reg_read(e->uc, UC_X86_REG_AX, &ax);
        uc_reg_read(e->uc, UC_X86_REG_DX, &dx);
        *dxax = (dx << 16) | (ax & 0xffff);
    }
    return 0;
}

int ne_init_module(nemu *e, nemod *m, uint16_t *axout) {
    if (m->entry_cs < 1 || m->entry_cs > m->nseg) return -1;
    uint32_t sp = 0xf000;
    sp -= 2; ne_wr16(e, e->stack_base + sp, e->trap_sel);
    sp -= 2; ne_wr16(e, e->stack_base + sp, 0);

    /* What Windows hands a library entry point: DI the module handle, DS the
       automatic data segment, CX the local heap size, ES:SI a command line
       that a DLL never reads. */
    uint32_t ss = e->stack_sel;
    uint32_t ds = (m->ds_seg >= 1 && m->ds_seg <= m->nseg)
                ? m->seg[m->ds_seg - 1].sel : e->stack_sel;
    uint32_t di = m->handle, cx = 0, si = 0, es = ds, bp = 0;
    uint32_t cs = m->seg[m->entry_cs - 1].sel, ip = m->entry_ip;

    uc_reg_write(e->uc, UC_X86_REG_SS, &ss);
    uc_reg_write(e->uc, UC_X86_REG_ESP, &sp);
    uc_reg_write(e->uc, UC_X86_REG_EBP, &bp);
    uc_reg_write(e->uc, UC_X86_REG_DS, &ds);
    uc_reg_write(e->uc, UC_X86_REG_ES, &es);
    uc_reg_write(e->uc, UC_X86_REG_DI, &di);
    uc_reg_write(e->uc, UC_X86_REG_SI, &si);
    uc_reg_write(e->uc, UC_X86_REG_CX, &cx);
    uc_reg_write(e->uc, UC_X86_REG_CS, &cs);
    uc_reg_write(e->uc, UC_X86_REG_IP, &ip);

    if (run(e) < 0) return -1;
    uint32_t ax = 0;
    uc_reg_read(e->uc, UC_X86_REG_AX, &ax);
    if (axout) *axout = (uint16_t)ax;
    return 0;
}

void ne_report(nemu *e) {
    fprintf(stderr, "host call counts:\n");
    for (int i = 0; i < e->nthunk; i++)
        if (e->ncalled[i])
            fprintf(stderr, "  %-10s %-24s %llu\n", e->thunk[i]->mod,
                    e->thunk[i]->name, (unsigned long long)e->ncalled[i]);
}
