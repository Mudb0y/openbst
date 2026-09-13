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
    uint32_t last_error;
    int      exited;

    FILE *trace;
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

/* shims.c */
extern const shim_def shim_table[];
extern const int      shim_table_len;
const shim_def *shim_lookup(const char *dll, const char *name);

#endif
