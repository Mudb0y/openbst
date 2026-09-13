#ifndef WINEMU_H
#define WINEMU_H

#include <stdint.h>
#include <stdio.h>
#include <unicorn/unicorn.h>

/* Guest address space. Chosen to sit clear of the DLL's 0x10000000 image base
   and of each other, with unmapped gaps between so a stray pointer faults
   instead of silently landing in something real. */
#define STACK_BASE    0x00200000u
#define STACK_SIZE    0x00100000u
#define HEAP_BASE     0x30000000u
#define HEAP_SIZE     0x08000000u
#define SCRATCH_BASE  0x60000000u
#define SCRATCH_SIZE  0x00010000u
#define SHIM_BASE     0x70000000u
#define SHIM_SIZE     0x00001000u
#define GDT_BASE      0x7fff0000u
#define TEB_BASE      0x7ffdf000u
#define PEB_BASE      0x7ffdd000u

/* Return address pushed before calling into guest code. Emulation halts when
   control reaches it, which is how we detect that the call returned. */
#define MAGIC_RET     0x0ff00000u

#define SHIM_STRIDE   8
#define MAX_IMPORTS   256
#define MAX_TLS       64

typedef struct emu emu;

/* A shim handler reads its arguments with emu_arg() and returns the value the
   guest should see in EAX. Stack cleanup is done by the `ret imm16` planted in
   the shim slot, so handlers never touch ESP. */
typedef uint32_t (*shim_fn)(emu *e, uint32_t esp);

typedef struct {
    const char *dll;
    const char *name;
    int         argc;
    shim_fn     fn;
} shim_def;

struct emu {
    uc_engine *uc;

    uint32_t image_base;
    uint32_t image_size;
    uint32_t entry;

    uint32_t heap_next;
    uint32_t scratch_next;

    /* Slot i of the shim page dispatches to handler[i]. */
    const shim_def *handler[MAX_IMPORTS];
    uint64_t        ncalled[MAX_IMPORTS];
    int             nshims;

    /* Captured waveOut output. */
    uint8_t *pcm;
    size_t   pcm_len, pcm_cap;
    uint32_t sample_rate;
    uint16_t channels, bits;
    int      wave_open;

    uint32_t tls[MAX_TLS];
    int      tls_used[MAX_TLS];

    uint32_t wndproc;
    uint32_t hwnd;

    /* Posted window messages, oldest first. The engine drives its own message
       pump and will not return until it sees its buffers complete. */
    struct { uint32_t hwnd, msg, wparam, lparam; } msgq[256];
    int msgq_head, msgq_tail;

    /* Set by a shim that wants guest code to run before the shim "returns".
       The hook stops emulation and emu_call resumes at the new EIP. */
    int      redirect;
    int      redirect_taken;
    uint32_t redirect_eip, redirect_esp;
    uint32_t last_error;
    int      exited;

    /* Guards against the engine spinning in a wait loop we have not satisfied.
       0 means no limit. */
    uint64_t insn_limit;

    FILE *trace;

    /* Write watch over a small address range, for following filter state. */
    FILE    *watch;
    uint32_t watch_lo, watch_hi;

    /* The engine's diagnostic buffer is addressed by a signed 16-bit index,
       so it cannot hold more than 32767 bytes. Long utterances overflow it.
       Draining copies the contents out and rewinds the index whenever it
       approaches the limit, which the append routine tolerates because it
       re-reads the index for every character. */
    /* Argument logging at a chosen function entry. */
    FILE    *calllog;
    uint32_t hook_pc;
    int      hook_nargs;

    /* Dumps the bytes a pointer argument points at, for following the
       segment records that drive frame generation. */
    FILE    *reclog;
    int      rec_argno, rec_nbytes;

    /* Snapshots chosen memory regions each time a chosen instruction runs.
       Two independent slots, so a value can be sampled before and after the
       code that changes it. */
    FILE    *regmemlog;
    uint32_t regmem_pc;
    int      regmem_reg, regmem_len;
    int32_t  regmem_off;

    FILE    *dumplog;
    struct {
        uint32_t pc;
        uint32_t addr[8];
        int      len[8], n;
        char     tag;
    } dump[2];
    int dump_slots;

    uint32_t drain_idx, drain_buf;
    int      drain_at;
    char    *acc;
    size_t   acc_len, acc_cap;

    /* A ring of the most recently executed instructions, dumped when a fault
       stops the run. A fault names only where control ended up; the ring names
       how it got there, which is the only way to find a stack clobber. */
    struct { uint32_t eip, esp, ebp; } ring[4096];
    int ring_at, ring_on;

    int   verbose;
};

/* winemu.c */
emu     *emu_new(void);
void     emu_free(emu *e);
int      emu_load_pe(emu *e, const char *path);
int      emu_call(emu *e, uint32_t func, const uint32_t *args, int argc, uint32_t *ret);
int      emu_call_dllmain(emu *e, uint32_t reason);
uint32_t emu_export(emu *e, const char *name);
uint32_t emu_alloc(emu *e, uint32_t size);
uint32_t emu_push_bytes(emu *e, const void *p, uint32_t n);
uint32_t emu_push_str(emu *e, const char *s);
uint32_t emu_arg(emu *e, uint32_t esp, int i);
uint32_t emu_rd32(emu *e, uint32_t addr);
void     emu_wr32(emu *e, uint32_t addr, uint32_t v);
int      emu_read(emu *e, uint32_t addr, void *dst, uint32_t n);
int      emu_write(emu *e, uint32_t addr, const void *src, uint32_t n);
void     emu_trace_reads(emu *e, FILE *out);
void     emu_watch_writes(emu *e, FILE *out, uint32_t lo, uint32_t hi);
void     emu_drain_setup(emu *e, uint32_t idx_addr, uint32_t buf_addr, int threshold, uint32_t after_pc);
void     emu_hook_call(emu *e, uint32_t pc, int nargs, FILE *out);
void     emu_hook_record(emu *e, uint32_t pc, int argno, int nbytes, FILE *out);
void     emu_hook_dump(emu *e, uint32_t pc, const uint32_t *addrs, const int *lens,
                       int n, char tag, FILE *out);
void     emu_hook_regs(emu *e, uint32_t pc, FILE *out);
void     emu_backtrace(emu *e);

/* Dumps memory at an address held in a register, which is how the engine
   reaches most of its per-segment state. reg is an index into the same order
   emu_hook_regs prints: eax ebx ecx edx esi edi ebp esp. */
void     emu_hook_regmem(emu *e, uint32_t pc, int reg, int32_t offset,
                         int nbytes, FILE *out);
void     emu_drain_flush(emu *e);
const char *emu_drained(emu *e, size_t *len);
void     emu_report_shims(emu *e);
void     emu_post(emu *e, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
int      emu_peek(emu *e, uint32_t *hwnd, uint32_t *msg, uint32_t *wp, uint32_t *lp, int remove);

/* shims.c */
extern const shim_def shim_table[];
extern const int      shim_table_len;
const shim_def *shim_lookup(const char *dll, const char *name);

#endif
