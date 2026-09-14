#ifndef NEEMU_H
#define NEEMU_H

#include <stdint.h>
#include <stdio.h>
#include <unicorn/unicorn.h>

/* A 16-bit protected-mode loader for the 1998 language family, which ships as
   Windows 3.x NE modules rather than PE ones. Unicorn has no notion of a
   module, a selector or a Win16 host, so all three are built here: a GDT with
   one descriptor per segment, a linear arena carved into 64K windows, and a
   page of far-return thunks standing in for KERNEL.

   The 32-bit oracle in winemu.c is left alone. Nothing is shared but the
   idea. */

#define NE_ARENA_BASE 0x01000000u
#define NE_ARENA_SIZE 0x04000000u
#define NE_WINDOW     0x00010000u      /* one selector addresses this much */
#define NE_GDT_BASE   0x7fff0000u
#define NE_MAX_SEL    512
#define NE_MAX_SEG    64
#define NE_MAX_MOD    8
#define NE_MAX_MODS   8
#define NE_MAX_THUNK  512

typedef struct nemu nemu;

/* A host shim reads its arguments with ne_argw and leaves its result in
   e->ax and e->dx. Stack cleanup is done by the `retf imm16` planted in the
   thunk slot, so shims never touch SP. */
typedef void (*ne_shim_fn)(nemu *e, uint32_t sp);

/* A host callback the guest calls through a far pointer we hand it. Far cdecl,
   so the guest cleans the stack and arguments read left to right from sp. */
typedef void (*ne_cb_fn)(nemu *e, uint32_t sp);

typedef struct {
    const char *mod;
    int         ordinal;
    const char *name;
    int         words;     /* argument words the callee pops */
    int         isconst;   /* an equate: the relocation takes a value, not a
                              far pointer */
    uint32_t    value;
    ne_shim_fn  fn;
} ne_import;

typedef struct {
    char     name[16];
    uint8_t *file;
    size_t   flen;
    uint32_t ne;
    int      nseg;
    struct {
        uint16_t sel;
        uint32_t base, len, alloc, flags;
    } seg[NE_MAX_SEG];
    int      nmod;
    char     imports[NE_MAX_MOD][16];
    uint16_t ds_seg;
    uint16_t entry_cs, entry_ip;
    struct { uint8_t seg; uint16_t off; } ent[512];
    int      nent;
    struct { char name[48]; int ord; } exp[128];
    int      nexp;
    uint16_t handle;       /* what GetModuleHandle and the entry's DI see */
} nemod;

struct nemu {
    uc_engine *uc;

    uint32_t arena_next;
    struct { uint32_t base, limit; int used; } sel[NE_MAX_SEL];

    nemod mod[NE_MAX_MODS];
    int   nmods;

    uint16_t thunk_sel;
    uint32_t thunk_base;
    int      nthunk;
    struct {
        const ne_import *imp;
        ne_cb_fn         cb;
        const char      *name;
    } thunk[NE_MAX_THUNK];
    uint64_t ncalled[NE_MAX_THUNK];

    uint16_t stack_sel;
    uint32_t stack_base;
    uint16_t stack_sp;
    uint16_t trap_sel;
    uint32_t trap_base;

    /* Global heap blocks. The handle is the selector, which is what Win16
       does for a fixed block and is indistinguishable to a caller that always
       locks before use. */
    struct { uint16_t sel; uint32_t size; int used; } gmem[256];
    int ngmem;

    uint16_t ax, dx;
    void    *user;
    int      stopped, faulted;
    int      verbose;
    uint64_t insn_limit;

    /* A ring of recently executed instructions, printed when a fault stops the
       run. A fault names where control ended up; the ring names how it got
       there. */
    struct { uint16_t cs, ip, sp; } ring[8192];
    int ring_at, ring_on;

    struct { uint32_t lin; int nstack; } probe[8];
    int nprobe;

    struct { uint32_t lin, base; uint16_t at[8]; int n; } peek[4];
    int npeek;

    /* One entry per instruction that reads the module's data, rather than one
       per read: a sentence makes hundreds of millions of reads and only a few
       hundred instructions make them. */
    FILE *trace;
    uint32_t watch_lo, watch_hi;
    int      watching;
    struct { uint32_t pc, lo, hi; uint64_t n; int order; } *tab;
    int    ntab, captab;
    int    tabseq;

    /* Captured PUTFR frames, which is what the synthesiser hands its caller
       in place of audio. */
    uint8_t *frames;
    size_t   frames_len, frames_cap;
};

nemu    *ne_new(void);
void     ne_free(nemu *e);

/* Loads a module, maps its segments, applies its relocations and records its
   exports. Does not run anything. */
nemod   *ne_load(nemu *e, const char *path);

/* Runs the module's own entry point with the register block Windows passes a
   DLL. Returns the AX the module leaves. */
int      ne_init_module(nemu *e, nemod *m, uint16_t *ax);

/* Runs subsequent calls on a different stack. The engine builds far pointers
   to its locals out of SS and compares them against pointers built out of DS,
   so it only works when the two are the same selector -- which means its
   stack has to live in its own data group, as an application's does. */
void     ne_set_stack(nemu *e, uint16_t sel, uint16_t sp);

/* Looks an export up by name and returns its selector and offset. */
int      ne_export(nemu *e, nemod *m, const char *name, uint16_t *seg, uint16_t *off);

/* Calls a far cdecl function: args[0] ends up at the lowest address, which is
   where the callee's [bp+6] looks. */
int      ne_call(nemu *e, uint16_t seg, uint16_t off,
                 const uint16_t *args, int nwords, uint32_t *dxax);

uint32_t ne_lin(nemu *e, uint16_t sel, uint16_t off);
uint16_t ne_argw(nemu *e, uint32_t sp, int nwords, int i);
/* Argument i of a far cdecl call, counting from the left. */
uint16_t ne_argc(nemu *e, uint32_t sp, int i);
uint32_t ne_argcd(nemu *e, uint32_t sp, int i);

/* Plants a far-callable thunk for a host function and returns it as
   selector:offset packed into one word pair. */
uint32_t ne_callback(nemu *e, ne_cb_fn fn, const char *name);
uint32_t ne_argd(nemu *e, uint32_t sp, int nwords, int i);
int      ne_read(nemu *e, uint32_t lin, void *dst, uint32_t n);
int      ne_write(nemu *e, uint32_t lin, const void *src, uint32_t n);
uint16_t ne_rd16(nemu *e, uint32_t lin);
void     ne_wr16(nemu *e, uint32_t lin, uint16_t v);
int      ne_str(nemu *e, uint32_t lin, char *dst, int cap);

/* Allocates a selector over `bytes` of fresh arena. */
uint16_t ne_alloc_sel(nemu *e, uint32_t bytes, int code);
uint16_t ne_global_alloc(nemu *e, uint32_t bytes);

void     ne_report(nemu *e);
void     ne_backtrace(nemu *e);

/* Prints the registers and a few stack words each time control reaches an
   address, which is how an argument list gets read without guessing. */
void     ne_hook_regs(nemu *e, uint16_t sel, uint16_t off, int nstack);

/* Prints chosen words of a segment each time control reaches an address,
   which is how a running value is followed without stepping. */
void     ne_hook_peek(nemu *e, uint16_t sel, uint16_t off,
                      uint16_t data, const uint16_t *addrs, int n);

/* Records every read of a loaded module's data, as twelve-byte records of
   address, program counter and size. Both addresses are in the same form the
   table directory uses, (segment << 16) | offset, so the analysis tools do not
   have to know where the emulator put anything. */
void     ne_trace_reads(nemu *e, FILE *out);

/* Reports every write into a range of one segment, with the instruction that
   made it. A read trace says which tables a stage consults; this says which
   instruction put a byte where, which is what a difference of one byte needs. */
void     ne_watch_writes(nemu *e, uint16_t seg, uint16_t lo, uint16_t hi);
void     ne_trace_report(nemu *e);
void     ne_frames_reset(nemu *e);

/* neshims.c */
extern const ne_import ne_import_table[];
extern const int       ne_import_table_len;
const ne_import *ne_import_find(const char *mod, int ordinal, const char *name);

#endif
