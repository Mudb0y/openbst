#include <stdlib.h>
#include <string.h>
#include "winemu.h"

static const int REGIDS[8] = {
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP
};

static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

int emu_read(emu *e, uint32_t addr, void *dst, uint32_t n) {
    return uc_mem_read(e->uc, addr, dst, n) == UC_ERR_OK ? 0 : -1;
}

int emu_write(emu *e, uint32_t addr, const void *src, uint32_t n) {
    /* Shim writes do not go through Unicorn's write hook, so the watch has to
       be applied here as well or a shim overrunning a guest buffer is the one
       kind of corruption the watch cannot see. */
    if (e->watch && addr < e->watch_hi && addr + n > e->watch_lo) {
        uint32_t pc = 0;
        uc_reg_read(e->uc, UC_X86_REG_EIP, &pc);
        fprintf(e->watch, "%08x %u shim %08x\n", addr, n, pc);
    }
    return uc_mem_write(e->uc, addr, src, n) == UC_ERR_OK ? 0 : -1;
}

uint32_t emu_rd32(emu *e, uint32_t addr) {
    uint32_t v = 0;
    uc_mem_read(e->uc, addr, &v, 4);
    return v;
}

void emu_wr32(emu *e, uint32_t addr, uint32_t v) {
    uc_mem_write(e->uc, addr, &v, 4);
}

/* Argument i of a stdcall shim. [esp] is the return address. */
uint32_t emu_arg(emu *e, uint32_t esp, int i) {
    return emu_rd32(e, esp + 4 + 4 * i);
}

uint32_t emu_alloc(emu *e, uint32_t size) {
    uint32_t p = e->heap_next;
    uint32_t n = align_up(size ? size : 1, 16);
    if (p + n > HEAP_BASE + HEAP_SIZE) return 0;
    e->heap_next = p + n;
    static const uint8_t zeros[4096] = { 0 };
    for (uint32_t off = 0; off < n; off += sizeof zeros) {
        uint32_t chunk = n - off < sizeof zeros ? n - off : sizeof zeros;
        uc_mem_write(e->uc, p + off, zeros, chunk);
    }
    return p;
}

uint32_t emu_push_bytes(emu *e, const void *p, uint32_t n) {
    uint32_t at = e->scratch_next;
    if (at + n > SCRATCH_BASE + SCRATCH_SIZE) return 0;
    uc_mem_write(e->uc, at, p, n);
    e->scratch_next = align_up(at + n, 8);
    return at;
}

uint32_t emu_push_str(emu *e, const char *s) {
    return emu_push_bytes(e, s, (uint32_t)strlen(s) + 1);
}

/* ---- descriptor tables ------------------------------------------------- */

/* Unicorn gives us a bare CPU, so flat segments have to be built by hand.
   Without this the CRT's fs:[0] SEH probe faults before DllMain is reached. */
static void set_desc(uint8_t *d, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
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

static void setup_segments(emu *e) {
    uint8_t gdt[8 * 8];
    memset(gdt, 0, sizeof gdt);
    set_desc(gdt + 1 * 8, 0, 0xffffffff, 0x9a, 0x4);          /* code */
    set_desc(gdt + 2 * 8, 0, 0xffffffff, 0x92, 0x4);          /* data */
    set_desc(gdt + 3 * 8, TEB_BASE, 0x0fff, 0x92, 0x4);       /* fs -> TEB */
    uc_mem_write(e->uc, GDT_BASE, gdt, sizeof gdt);

    uc_x86_mmr gdtr = { 0, GDT_BASE, sizeof gdt - 1, 0 };
    uc_reg_write(e->uc, UC_X86_REG_GDTR, &gdtr);

    int cs = 0x08, ds = 0x10, fs = 0x18;
    uc_reg_write(e->uc, UC_X86_REG_CS, &cs);
    uc_reg_write(e->uc, UC_X86_REG_DS, &ds);
    uc_reg_write(e->uc, UC_X86_REG_ES, &ds);
    uc_reg_write(e->uc, UC_X86_REG_SS, &ds);
    uc_reg_write(e->uc, UC_X86_REG_FS, &fs);
}

static void setup_teb(emu *e) {
    emu_wr32(e, TEB_BASE + 0x00, 0xffffffff);             /* ExceptionList */
    emu_wr32(e, TEB_BASE + 0x04, STACK_BASE + STACK_SIZE); /* StackBase */
    emu_wr32(e, TEB_BASE + 0x08, STACK_BASE);              /* StackLimit */
    emu_wr32(e, TEB_BASE + 0x18, TEB_BASE);                /* Self */
    emu_wr32(e, TEB_BASE + 0x20, 0x1000);                  /* ClientId.Process */
    emu_wr32(e, TEB_BASE + 0x24, 0x2000);                  /* ClientId.Thread */
    emu_wr32(e, TEB_BASE + 0x30, PEB_BASE);                /* ProcessEnvironmentBlock */
    emu_wr32(e, PEB_BASE + 0x08, 0x10000000);              /* ImageBaseAddress */
}

/* ---- shim dispatch ----------------------------------------------------- */

static void hook_shim(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)size;
    emu *e = ud;
    int idx = (int)((addr - SHIM_BASE) / SHIM_STRIDE);
    if (idx < 0 || idx >= e->nshims || !e->handler[idx]) return;

    e->ncalled[idx]++;
    uint32_t esp = 0;
    uc_reg_read(e->uc, UC_X86_REG_ESP, &esp);
    if (e->verbose > 1) {
        uint8_t sl[3] = {0,0,0};
        uc_mem_read(e->uc, (uint32_t)addr, sl, 3);
        fprintf(stderr, "  [shim] %-24s esp %08x ret %08x slot %02x %02x %02x\n",
                e->handler[idx]->name, esp, emu_rd32(e, esp), sl[0], sl[1], sl[2]);
    }
    uint32_t ret = e->handler[idx]->fn(e, esp);
    if (e->redirect) {
        /* Hand control to guest code instead of letting the planted ret run.
           The shim has already arranged a stack frame that returns to the
           shim's own caller afterwards. */
        e->redirect = 0;
        e->redirect_taken = 1;
        uc_reg_write(e->uc, UC_X86_REG_EIP, &e->redirect_eip);
        uc_reg_write(e->uc, UC_X86_REG_ESP, &e->redirect_esp);
        uc_emu_stop(e->uc);
        return;
    }
    uc_reg_write(e->uc, UC_X86_REG_EAX, &ret);
    /* The `ret imm16` planted in this slot performs the stdcall cleanup. */
}

void emu_post(emu *e, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    int n = (int)(sizeof e->msgq / sizeof e->msgq[0]);
    int next = (e->msgq_tail + 1) % n;
    if (next == e->msgq_head) return;   /* queue full: drop, as Windows would */
    e->msgq[e->msgq_tail].hwnd = hwnd;
    e->msgq[e->msgq_tail].msg = msg;
    e->msgq[e->msgq_tail].wparam = wp;
    e->msgq[e->msgq_tail].lparam = lp;
    e->msgq_tail = next;
}

int emu_peek(emu *e, uint32_t *hwnd, uint32_t *msg, uint32_t *wp, uint32_t *lp, int remove) {
    int n = (int)(sizeof e->msgq / sizeof e->msgq[0]);
    if (e->msgq_head == e->msgq_tail) return 0;
    *hwnd = e->msgq[e->msgq_head].hwnd;
    *msg  = e->msgq[e->msgq_head].msg;
    *wp   = e->msgq[e->msgq_head].wparam;
    *lp   = e->msgq[e->msgq_head].lparam;
    if (remove) e->msgq_head = (e->msgq_head + 1) % n;
    return 1;
}

static void hook_read(uc_engine *uc, uc_mem_type t, uint64_t addr,
                      int size, int64_t val, void *ud) {
    (void)uc; (void)t; (void)val;
    emu *e = ud;
    if (!e->trace) return;
    if (addr < e->image_base || addr >= e->image_base + e->image_size) return;
    uint32_t pc = 0;
    uc_reg_read(e->uc, UC_X86_REG_EIP, &pc);
    struct { uint32_t a, p; uint8_t s, pad[3]; } rec = { (uint32_t)addr, pc, (uint8_t)size, {0,0,0} };
    fwrite(&rec, sizeof rec, 1, e->trace);
}

static void hook_ring(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)size;
    emu *e = ud;
    int n = (int)(sizeof e->ring / sizeof e->ring[0]);
    e->ring[e->ring_at].eip = (uint32_t)addr;
    uc_reg_read(e->uc, UC_X86_REG_ESP, &e->ring[e->ring_at].esp);
    uc_reg_read(e->uc, UC_X86_REG_EBP, &e->ring[e->ring_at].ebp);
    e->ring_at = (e->ring_at + 1) % n;
}

void emu_backtrace(emu *e) {
    e->ring_on = 1;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_ring, e, 1, 0);
}

static bool hook_unmapped(uc_engine *uc, uc_mem_type t, uint64_t addr,
                          int size, int64_t val, void *ud) {
    (void)uc; (void)size; (void)val;
    emu *e = ud;
    uint32_t pc = 0;
    uc_reg_read(e->uc, UC_X86_REG_EIP, &pc);
    fprintf(stderr, "unmapped %s at 0x%08llx from eip 0x%08x\n",
            t == UC_MEM_READ_UNMAPPED ? "read" :
            t == UC_MEM_WRITE_UNMAPPED ? "write" : "fetch",
            (unsigned long long)addr, pc);
    /* On a bad fetch the stack top is the return address of whoever jumped
       here, which names the caller far faster than a single-step trace. */
    if (t == UC_MEM_FETCH_UNMAPPED) {
        uint32_t esp = 0;
        uc_reg_read(e->uc, UC_X86_REG_ESP, &esp);
        fprintf(stderr, "  called from 0x%08x (stack also holds 0x%08x 0x%08x)\n",
                emu_rd32(e, esp), emu_rd32(e, esp + 4), emu_rd32(e, esp + 8));
    }
    if (e->ring_on) {
        int n = (int)(sizeof e->ring / sizeof e->ring[0]);
        fprintf(stderr, "  last instructions, oldest first:\n");
        for (int i = 0; i < n; i++) {
            int k = (e->ring_at + i) % n;
            if (!e->ring[k].eip) continue;
            fprintf(stderr, "    %08x esp %08x ebp %08x\n",
                    e->ring[k].eip, e->ring[k].esp, e->ring[k].ebp);
        }
    }
    return false;
}

static void hook_write(uc_engine *uc, uc_mem_type t, uint64_t addr,
                       int size, int64_t val, void *ud) {
    (void)uc; (void)t;
    emu *e = ud;
    if (!e->watch || addr < e->watch_lo || addr >= e->watch_hi) return;
    uint32_t pc = 0;
    uc_reg_read(e->uc, UC_X86_REG_EIP, &pc);
    fprintf(e->watch, "%08x %d %lld %08x\n", (uint32_t)addr, size,
            (long long)val, pc);
}

void emu_trace_reads(emu *e, FILE *out) {
    e->trace = out;
}

static void acc_append(emu *e, const char *p, size_t n) {
    if (e->acc_len + n + 1 > e->acc_cap) {
        size_t cap = e->acc_cap ? e->acc_cap * 2 : (1 << 16);
        while (cap < e->acc_len + n + 1) cap *= 2;
        char *q = realloc(e->acc, cap);
        if (!q) return;
        e->acc = q;
        e->acc_cap = cap;
    }
    memcpy(e->acc + e->acc_len, p, n);
    e->acc_len += n;
    e->acc[e->acc_len] = 0;
}

/* Hooked on the instruction immediately after the append routine bumps its
   index. A write hook cannot do this job: it runs before the write commits,
   so a rewind issued there is overwritten by the increment landing, and the
   buffer gets copied out again for every subsequent character. */
static void hook_drain(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    emu *e = ud;
    if (!e->drain_at) return;

    uint16_t idx = 0;
    uc_mem_read(e->uc, e->drain_idx, &idx, 2);
    if (idx < e->drain_at) return;

    char *tmp = malloc(idx);
    if (tmp && uc_mem_read(e->uc, e->drain_buf, tmp, idx) == UC_ERR_OK)
        acc_append(e, tmp, idx);
    free(tmp);

    uint16_t zero = 0;
    uc_mem_write(e->uc, e->drain_idx, &zero, 2);
}

/* Logs the stack arguments each time control reaches a chosen entry point.
   Reading the arguments beats inferring them from a data trace, because the
   index arithmetic that produced them is often several instructions of
   lea-chains that are easy to misread. */
static void hook_call(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    emu *e = ud;
    if (!e->calllog) return;
    uint32_t esp = 0;
    uc_reg_read(e->uc, UC_X86_REG_ESP, &esp);
    fprintf(e->calllog, "call");
    for (int i = 0; i < e->hook_nargs; i++)
        fprintf(e->calllog, " %d", (int32_t)emu_rd32(e, esp + 4 + 4 * i));
    fprintf(e->calllog, "\n");
}

static void hook_record(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    emu *e = ud;
    if (!e->reclog) return;
    uint32_t esp = 0;
    uc_reg_read(e->uc, UC_X86_REG_ESP, &esp);
    uint32_t p = emu_rd32(e, esp + 4 + 4 * e->rec_argno);
    if (!p) return;
    uint8_t buf[64];
    int n = e->rec_nbytes > (int)sizeof buf ? (int)sizeof buf : e->rec_nbytes;
    if (uc_mem_read(e->uc, p, buf, n) != UC_ERR_OK) return;
    fprintf(e->reclog, "R");
    for (int i = 0; i < n; i++) fprintf(e->reclog, " %02X", buf[i]);
    fprintf(e->reclog, "\n");
}

void emu_hook_record(emu *e, uint32_t pc, int argno, int nbytes, FILE *out) {
    e->reclog = out;
    e->rec_argno = argno;
    e->rec_nbytes = nbytes;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_record, e, pc, pc);
}

static void hook_dump(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)size;
    emu *e = ud;
    if (!e->dumplog) return;
    for (int s = 0; s < e->dump_slots; s++) {
        if (e->dump[s].pc != (uint32_t)addr) continue;
        fprintf(e->dumplog, "%c", e->dump[s].tag);
        for (int r = 0; r < e->dump[s].n; r++) {
            uint8_t buf[128];
            int n = e->dump[s].len[r] > (int)sizeof buf ? (int)sizeof buf : e->dump[s].len[r];
            if (uc_mem_read(e->uc, e->dump[s].addr[r], buf, n) != UC_ERR_OK) continue;
            fprintf(e->dumplog, " |");
            for (int i = 0; i < n; i++) fprintf(e->dumplog, " %02X", buf[i]);
        }
        fprintf(e->dumplog, "\n");
    }
}

void emu_hook_dump(emu *e, uint32_t pc, const uint32_t *addrs, const int *lens,
                   int n, char tag, FILE *out) {
    if (e->dump_slots >= 2) return;
    int s = e->dump_slots++;
    e->dumplog = out;
    e->dump[s].pc = pc;
    e->dump[s].tag = tag;
    e->dump[s].n = n > 8 ? 8 : n;
    for (int i = 0; i < e->dump[s].n; i++) {
        e->dump[s].addr[i] = addrs[i];
        e->dump[s].len[i] = lens[i];
    }
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_dump, e, pc, pc);
}

static void hook_regs(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    emu *e = ud;
    if (!e->dumplog) return;
    const int *ids = REGIDS;
    const char *nm[8] = { "eax","ebx","ecx","edx","esi","edi","ebp","esp" };
    fprintf(e->dumplog, "G");
    for (int i = 0; i < 8; i++) {
        uint32_t v = 0;
        uc_reg_read(e->uc, ids[i], &v);
        fprintf(e->dumplog, " %s=%u", nm[i], v);
    }
    fprintf(e->dumplog, "\n");
}

static void hook_regmem(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)addr; (void)size;
    emu *e = ud;
    if (!e->regmemlog) return;
    uint32_t base = 0;
    uc_reg_read(e->uc, REGIDS[e->regmem_reg & 7], &base);
    uint8_t buf[64];
    int n = e->regmem_len > (int)sizeof buf ? (int)sizeof buf : e->regmem_len;
    if (uc_mem_read(e->uc, base + e->regmem_off, buf, n) != UC_ERR_OK) return;
    fprintf(e->regmemlog, "M %08x", base + e->regmem_off);
    for (int i = 0; i < n; i++) fprintf(e->regmemlog, " %02X", buf[i]);
    fprintf(e->regmemlog, "\n");
}

void emu_hook_regmem(emu *e, uint32_t pc, int reg, int32_t offset,
                     int nbytes, FILE *out) {
    e->regmemlog = out;
    e->regmem_pc = pc;
    e->regmem_reg = reg;
    e->regmem_off = offset;
    e->regmem_len = nbytes;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_regmem, e, pc, pc);
}

void emu_hook_regs(emu *e, uint32_t pc, FILE *out) {
    e->dumplog = out;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_regs, e, pc, pc);
}

void emu_hook_call(emu *e, uint32_t pc, int nargs, FILE *out) {
    e->calllog = out;
    e->hook_pc = pc;
    e->hook_nargs = nargs;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_call, e, pc, pc);
}

void emu_drain_setup(emu *e, uint32_t idx_addr, uint32_t buf_addr, int threshold,
                     uint32_t after_pc) {
    e->drain_idx = idx_addr;
    e->drain_buf = buf_addr;
    e->drain_at = threshold;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_drain, e, after_pc, after_pc);
}

/* Takes whatever is left in the guest buffer. Uses the terminator rather than
   the index, because GetPhBuf zeroes the index on its way out. */
void emu_drain_flush(emu *e) {
    if (!e->drain_at) return;
    char *tmp = malloc(e->drain_at + 1);
    if (!tmp) return;
    if (uc_mem_read(e->uc, e->drain_buf, tmp, e->drain_at) == UC_ERR_OK) {
        tmp[e->drain_at] = 0;
        acc_append(e, tmp, strlen(tmp));
    }
    free(tmp);
}

const char *emu_drained(emu *e, size_t *len) {
    if (len) *len = e->acc_len;
    return e->acc ? e->acc : "";
}

void emu_watch_writes(emu *e, FILE *out, uint32_t lo, uint32_t hi) {
    e->watch = out;
    e->watch_lo = lo;
    e->watch_hi = hi;
    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_MEM_WRITE, hook_write, e, lo, hi - 1);
}

void emu_report_shims(emu *e) {
    fprintf(stderr, "shim call counts:\n");
    for (int i = 0; i < e->nshims; i++)
        if (e->ncalled[i])
            fprintf(stderr, "  %-26s %llu\n", e->handler[i]->name,
                    (unsigned long long)e->ncalled[i]);
}

/* ---- construction ------------------------------------------------------ */

emu *emu_new(void) {
    emu *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &e->uc) != UC_ERR_OK) { free(e); return NULL; }

    uc_mem_map(e->uc, STACK_BASE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(e->uc, HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(e->uc, SCRATCH_BASE, SCRATCH_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(e->uc, SHIM_BASE, SHIM_SIZE, UC_PROT_READ | UC_PROT_EXEC);
    uc_mem_map(e->uc, GDT_BASE, 0x1000, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(e->uc, PEB_BASE, 0x3000, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_map(e->uc, MAGIC_RET & ~0xfffu, 0x1000, UC_PROT_READ | UC_PROT_EXEC);

    e->heap_next = HEAP_BASE;
    e->scratch_next = SCRATCH_BASE;
    e->sample_rate = 11025;
    e->channels = 1;
    e->bits = 16;

    setup_segments(e);
    setup_teb(e);

    uc_hook h;
    uc_hook_add(e->uc, &h, UC_HOOK_CODE, hook_shim, e, SHIM_BASE, SHIM_BASE + SHIM_SIZE - 1);
    uc_hook_add(e->uc, &h, UC_HOOK_MEM_READ, hook_read, e, 1, 0);
    uc_hook_add(e->uc, &h, UC_HOOK_MEM_INVALID, hook_unmapped, e, 1, 0);
    return e;
}

void emu_free(emu *e) {
    if (!e) return;
    if (e->uc) uc_close(e->uc);
    free(e->pcm);
    free(e->acc);
    free(e);
}

/* ---- PE loading -------------------------------------------------------- */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int bind_imports(emu *e, const uint8_t *f, size_t flen,
                        uint32_t imp_rva, const uint8_t *img, uint32_t imgsz);

int emu_load_pe(emu *e, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(fp, 0, SEEK_END);
    long flen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *f = malloc(flen);
    if (!f || fread(f, 1, flen, fp) != (size_t)flen) { fprintf(stderr, "read failed\n"); fclose(fp); free(f); return -1; }
    fclose(fp);

    if (rd16(f) != 0x5a4d) { fprintf(stderr, "not MZ\n"); free(f); return -1; }
    uint32_t pe = rd32(f + 0x3c);
    if (rd32(f + pe) != 0x00004550) { fprintf(stderr, "not PE\n"); free(f); return -1; }

    uint32_t opt = pe + 24;
    uint16_t nsec = rd16(f + pe + 6);
    uint16_t opthdr = rd16(f + pe + 20);
    e->image_base = rd32(f + opt + 28);
    e->image_size = rd32(f + opt + 56);
    e->entry = e->image_base + rd32(f + opt + 16);

    uint8_t *img = calloc(1, e->image_size);
    if (!img) { free(f); return -1; }

    uint32_t hdrsz = rd32(f + opt + 60);
    memcpy(img, f, hdrsz < e->image_size ? hdrsz : e->image_size);

    uint32_t sectab = opt + opthdr;
    for (int i = 0; i < nsec; i++) {
        const uint8_t *s = f + sectab + i * 40;
        uint32_t vs = rd32(s + 8), va = rd32(s + 12), rs = rd32(s + 16), ro = rd32(s + 20);
        uint32_t n = rs < vs ? rs : vs;
        if (va + n <= e->image_size && ro + n <= (uint32_t)flen)
            memcpy(img + va, f + ro, n);
    }

    uint32_t mapsz = align_up(e->image_size, 0x1000);
    uc_mem_map(e->uc, e->image_base, mapsz, UC_PROT_ALL);
    uc_mem_write(e->uc, e->image_base, img, e->image_size);

    uint32_t imp_rva = rd32(f + opt + 104);
    int rc = bind_imports(e, f, flen, imp_rva, img, e->image_size);

    free(img);
    free(f);
    return rc;
}

static const char *img_str(const uint8_t *img, uint32_t imgsz, uint32_t rva) {
    if (rva >= imgsz) return NULL;
    return (const char *)(img + rva);
}

static int bind_imports(emu *e, const uint8_t *f, size_t flen,
                        uint32_t imp_rva, const uint8_t *img, uint32_t imgsz) {
    (void)f; (void)flen;
    if (!imp_rva || imp_rva >= imgsz) return 0;

    uint8_t slots[SHIM_SIZE];
    memset(slots, 0xcc, sizeof slots);
    int nsh = 0;
    int missing = 0;

    for (uint32_t d = imp_rva;; d += 20) {
        uint32_t oft = rd32(img + d), namerva = rd32(img + d + 12), fthunk = rd32(img + d + 16);
        if (!namerva) break;
        const char *dllname = img_str(img, imgsz, namerva);
        uint32_t tin = oft ? oft : fthunk;

        for (int j = 0;; j++) {
            uint32_t v = rd32(img + tin + 4 * j);
            if (!v) break;
            const char *fname = (v & 0x80000000u) ? NULL : img_str(img, imgsz, (v & 0x7fffffffu) + 2);
            const shim_def *sd = fname ? shim_lookup(dllname, fname) : NULL;

            if (nsh >= MAX_IMPORTS) { fprintf(stderr, "too many imports\n"); return -1; }
            uint32_t slot = SHIM_BASE + (uint32_t)nsh * SHIM_STRIDE;

            if (!sd) {
                missing++;
                fprintf(stderr, "unimplemented import %s!%s\n", dllname, fname ? fname : "(ordinal)");
                /* Leave an int3 so a call to it stops rather than corrupts. */
            } else {
                uint8_t *p = slots + nsh * SHIM_STRIDE;
                if (sd->argc == 0) {
                    p[0] = 0xc3;
                } else {
                    uint16_t pop = (uint16_t)(sd->argc * 4);
                    p[0] = 0xc2; p[1] = pop & 0xff; p[2] = pop >> 8;
                }
            }
            if (e->verbose > 1)
                fprintf(stderr, "bind %08x %s!%s\n",
                        e->image_base + fthunk + 4 * j, dllname,
                        fname ? fname : "(ordinal)");
            e->handler[nsh] = sd;
            nsh++;
            emu_wr32(e, e->image_base + fthunk + 4 * j, slot);
        }
    }

    e->nshims = nsh;
    uc_mem_write(e->uc, SHIM_BASE, slots, sizeof slots);
    if (missing) fprintf(stderr, "%d import(s) unimplemented\n", missing);
    return 0;
}

uint32_t emu_export(emu *e, const char *name) {
    uint32_t opt_rva = 0, sz = 0;
    /* Re-read the export directory straight from guest memory. */
    uint32_t pe = emu_rd32(e, e->image_base + 0x3c);
    uint32_t opt = e->image_base + pe + 24;
    opt_rva = emu_rd32(e, opt + 96);
    sz = emu_rd32(e, opt + 100);
    if (!opt_rva || !sz) return 0;

    uint32_t dir = e->image_base + opt_rva;
    uint32_t nnames = emu_rd32(e, dir + 24);
    uint32_t afunc = emu_rd32(e, dir + 28);
    uint32_t aname = emu_rd32(e, dir + 32);
    uint32_t aord = emu_rd32(e, dir + 36);

    for (uint32_t i = 0; i < nnames; i++) {
        uint32_t nrva = emu_rd32(e, e->image_base + aname + 4 * i);
        char buf[128];
        for (size_t k = 0; k < sizeof buf; k++) {
            uint8_t c = 0;
            uc_mem_read(e->uc, e->image_base + nrva + k, &c, 1);
            buf[k] = (char)c;
            if (!c) break;
        }
        buf[sizeof buf - 1] = 0;
        if (strcmp(buf, name) == 0) {
            uint16_t ord = 0;
            uc_mem_read(e->uc, e->image_base + aord + 2 * i, &ord, 2);
            return e->image_base + emu_rd32(e, e->image_base + afunc + 4 * ord);
        }
    }
    return 0;
}

/* ---- calling into guest code ------------------------------------------- */

int emu_call(emu *e, uint32_t func, const uint32_t *args, int argc, uint32_t *ret) {
    uint32_t esp = STACK_BASE + STACK_SIZE - 0x1000;
    for (int i = argc - 1; i >= 0; i--) {
        esp -= 4;
        emu_wr32(e, esp, args[i]);
    }
    esp -= 4;
    emu_wr32(e, esp, MAGIC_RET);
    uc_reg_write(e->uc, UC_X86_REG_ESP, &esp);

    /* Emulation is restarted after every shim that redirects into guest code,
       because Unicorn cannot be re-entered from inside a hook. */
    uint32_t start = func, pc = 0;
    for (long seg = 0;; seg++) {
        uc_err err = uc_emu_start(e->uc, start, MAGIC_RET, 0, e->insn_limit);
        uc_reg_read(e->uc, UC_X86_REG_EIP, &pc);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "emu error at 0x%08x: %s\n", pc, uc_strerror(err));
            return -1;
        }
        if (pc == MAGIC_RET || e->exited) break;
        if (seg > 4000000L) {
            fprintf(stderr, "too many resumes, still running at 0x%08x\n", pc);
            return -2;
        }
        if (e->insn_limit && !e->redirect_taken) {
            fprintf(stderr, "instruction budget exhausted, still running at 0x%08x\n", pc);
            return -2;
        }
        e->redirect_taken = 0;
        start = pc;
    }
    if (ret) uc_reg_read(e->uc, UC_X86_REG_EAX, ret);
    return 0;
}

int emu_call_dllmain(emu *e, uint32_t reason) {
    uint32_t args[3] = { e->image_base, reason, 0 };
    uint32_t r = 0;
    if (emu_call(e, e->entry, args, 3, &r) < 0) return -1;
    if (e->verbose) fprintf(stderr, "DllMain(reason=%u) -> %u\n", reason, r);
    return r ? 0 : -1;
}
