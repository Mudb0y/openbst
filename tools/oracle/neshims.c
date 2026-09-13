#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "neemu.h"

/* The Win16 host, as much of it as the 1998 engine touches. Every entry is a
   far-callable thunk whose slot holds the `retf imm16` that performs the
   pascal cleanup, so the handlers below only ever produce a value. */

static void ret16(nemu *e, uint16_t v) { e->ax = v; e->dx = 0; }
static void ret32(nemu *e, uint32_t v) { e->ax = (uint16_t)v; e->dx = (uint16_t)(v >> 16); }

/* Strips a path and an extension so "C:\\WINDOWS\\KGMENG.DLL" matches the
   module name the NE header carries. */
static void basename_noext(const char *in, char *out, int cap) {
    const char *p = strrchr(in, '\\');
    const char *q = strrchr(in, '/');
    if (q > p) p = q;
    p = p ? p + 1 : in;
    int i = 0;
    for (; p[i] && p[i] != '.' && i < cap - 1; i++) out[i] = p[i];
    out[i] = 0;
}

static nemod *find_module(nemu *e, const char *name) {
    char want[64];
    basename_noext(name, want, sizeof want);
    for (int i = 0; i < e->nmods; i++)
        if (strcasecmp(e->mod[i].name, want) == 0) return &e->mod[i];
    return NULL;
}

static nemod *by_handle(nemu *e, uint16_t h) {
    for (int i = 0; i < e->nmods; i++)
        if (e->mod[i].handle == h) return &e->mod[i];
    return NULL;
}

/* ---- termination -------------------------------------------------------- */

static void s_FatalExit(nemu *e, uint32_t sp) {
    fprintf(stderr, "ne: FatalExit(%d)\n", (int16_t)ne_argw(e, sp, 1, 0));
    e->faulted = 1;
    uc_emu_stop(e->uc);
}

static void s_FatalAppExit(nemu *e, uint32_t sp) {
    char msg[256];
    uint32_t p = ne_lin(e, ne_argw(e, sp, 3, 2), ne_argw(e, sp, 3, 1));
    ne_str(e, p, msg, sizeof msg);
    fprintf(stderr, "ne: FatalAppExit: %s\n", msg);
    e->faulted = 1;
    uc_emu_stop(e->uc);
}

static void s_ExitKernel(nemu *e, uint32_t sp) {
    (void)sp;
    e->stopped = 1;
    uc_emu_stop(e->uc);
}

/* ---- version and environment -------------------------------------------- */

static void s_GetVersion(nemu *e, uint32_t sp) {
    (void)sp;
    /* Low byte major, high byte minor: Windows 3.10, under DOS 6.22. */
    e->ax = 0x0a03;
    e->dx = 0x1606;
}

static void s_GetWinFlags(nemu *e, uint32_t sp) { (void)sp; ret32(e, 0x413); }

static void s_GetDOSEnvironment(nemu *e, uint32_t sp) {
    (void)sp;
    static uint16_t sel;
    if (!sel) {
        sel = ne_alloc_sel(e, 256, 0);
        static const char env[] = "PATH=C:\\\0\0";
        ne_write(e, ne_lin(e, sel, 0), env, sizeof env);
    }
    e->ax = 0;
    e->dx = sel;
}

static void s_SetErrorMode(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

/* ---- the global heap ---------------------------------------------------- */

static void s_GlobalAlloc(nemu *e, uint32_t sp) {
    uint32_t bytes = ne_argd(e, sp, 3, 1);
    if (!bytes) bytes = 1;
    ret16(e, ne_global_alloc(e, bytes));
}

static void s_GlobalReAlloc(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 4, 0);
    uint32_t bytes = ne_argd(e, sp, 4, 1);
    for (int i = 0; i < e->ngmem; i++) {
        if (e->gmem[i].sel != h || !e->gmem[i].used) continue;
        if (bytes <= e->gmem[i].size) { ret16(e, h); return; }
        uint16_t n = ne_global_alloc(e, bytes);
        if (!n) { ret16(e, 0); return; }
        uint8_t *tmp = malloc(e->gmem[i].size);
        if (tmp) {
            ne_read(e, ne_lin(e, h, 0), tmp, e->gmem[i].size);
            ne_write(e, ne_lin(e, n, 0), tmp, e->gmem[i].size);
            free(tmp);
        }
        e->gmem[i].used = 0;
        ret16(e, n);
        return;
    }
    ret16(e, 0);
}

static void s_GlobalFree(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 1, 0);
    for (int i = 0; i < e->ngmem; i++)
        if (e->gmem[i].sel == h) e->gmem[i].used = 0;
    ret16(e, 0);
}

static void s_GlobalLock(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 1, 0);
    e->ax = 0;
    e->dx = h;                      /* a fixed block: the handle is a selector */
}

static void s_GlobalUnlock(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

static void s_GlobalSize(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 1, 0);
    for (int i = 0; i < e->ngmem; i++)
        if (e->gmem[i].sel == h && e->gmem[i].used) { ret32(e, e->gmem[i].size); return; }
    ret32(e, 0);
}

static void s_GlobalHandle(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 1, 0);
    e->ax = h;
    e->dx = h;
}

static void s_LockSegment(nemu *e, uint32_t sp) { ret16(e, ne_argw(e, sp, 1, 0)); }
static void s_UnlockSegment(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

static void s_GlobalDOSAlloc(nemu *e, uint32_t sp) {
    uint32_t bytes = ne_argd(e, sp, 2, 0);
    uint16_t sel = ne_global_alloc(e, bytes ? bytes : 1);
    e->ax = sel;
    e->dx = sel;                    /* no real-mode paragraph to report */
}

static void s_GlobalDOSFree(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

/* ---- the local heap ----------------------------------------------------- */

/* The two modules initialise a local heap but never allocate from it through
   KERNEL, so a heap that reports success is enough. */
static void s_LocalInit(nemu *e, uint32_t sp) { (void)sp; ret16(e, 1); }

/* ---- selectors ---------------------------------------------------------- */

static void s_AllocDStoCSAlias(nemu *e, uint32_t sp) {
    /* Unicorn's protection is per page, not per descriptor, so a data selector
       is already executable and can stand in for its own alias. */
    ret16(e, ne_argw(e, sp, 1, 0));
}

static void s_FreeSelector(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

/* ---- modules ------------------------------------------------------------ */

static void s_GetModuleUsage(nemu *e, uint32_t sp) { (void)sp; ret16(e, 1); }

static void s_GetModuleHandle(nemu *e, uint32_t sp) {
    char name[128];
    ne_str(e, ne_lin(e, ne_argw(e, sp, 2, 1), ne_argw(e, sp, 2, 0)), name, sizeof name);
    nemod *m = find_module(e, name);
    ret16(e, m ? m->handle : 0);
}

static void s_GetModuleFileName(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 4, 0);
    uint32_t buf = ne_lin(e, ne_argw(e, sp, 4, 2), ne_argw(e, sp, 4, 1));
    int cap = (int16_t)ne_argw(e, sp, 4, 3);
    nemod *m = by_handle(e, h);
    char path[128];
    snprintf(path, sizeof path, "C:\\WINDOWS\\%s.DLL", m ? m->name : "UNKNOWN");
    int n = (int)strlen(path);
    if (n > cap - 1) n = cap - 1;
    if (n < 0) n = 0;
    ne_write(e, buf, path, n);
    uint8_t z = 0;
    ne_write(e, buf + n, &z, 1);
    ret16(e, (uint16_t)n);
}

static void s_LoadLibrary(nemu *e, uint32_t sp) {
    char name[128];
    ne_str(e, ne_lin(e, ne_argw(e, sp, 2, 1), ne_argw(e, sp, 2, 0)), name, sizeof name);
    nemod *m = find_module(e, name);
    if (!m) {
        fprintf(stderr, "ne: LoadLibrary(%s) but it was not preloaded\n", name);
        ret16(e, 0);
        return;
    }
    ret16(e, m->handle);
}

static void s_FreeLibrary(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

static void s_GetProcAddress(nemu *e, uint32_t sp) {
    uint16_t h = ne_argw(e, sp, 3, 0);
    char name[128];
    ne_str(e, ne_lin(e, ne_argw(e, sp, 3, 2), ne_argw(e, sp, 3, 1)), name, sizeof name);
    nemod *m = by_handle(e, h);
    uint16_t seg = 0, off = 0;
    if (m && ne_export(e, m, name, &seg, &off) < 0) {
        char alt[130];
        snprintf(alt, sizeof alt, "_%s", name);
        if (ne_export(e, m, alt, &seg, &off) < 0) seg = 0;
    }
    if (!seg) {
        fprintf(stderr, "ne: GetProcAddress(%s) not found\n", name);
        ret32(e, 0);
        return;
    }
    e->ax = off;
    e->dx = seg;
}

static void s_MakeProcInstance(nemu *e, uint32_t sp) {
    e->ax = ne_argw(e, sp, 3, 0);   /* the far pointer, unchanged */
    e->dx = ne_argw(e, sp, 3, 1);
}

static void s_FreeProcInstance(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

static void s_InitTask(nemu *e, uint32_t sp) { (void)sp; ret16(e, 0); }

/* ---- profile ------------------------------------------------------------ */

static void s_GetPrivateProfileString(nemu *e, uint32_t sp) {
    /* Six arguments, eleven words: section, key, default, buffer, size, file.
       Nothing here has an INI file, so every key takes its default. */
    uint32_t def = ne_lin(e, ne_argw(e, sp, 11, 5), ne_argw(e, sp, 11, 4));
    uint32_t buf = ne_lin(e, ne_argw(e, sp, 11, 7), ne_argw(e, sp, 11, 6));
    int cap = (int16_t)ne_argw(e, sp, 11, 8);
    char tmp[256];
    ne_str(e, def, tmp, sizeof tmp);
    int n = (int)strlen(tmp);
    if (n > cap - 1) n = cap - 1;
    if (n < 0) n = 0;
    ne_write(e, buf, tmp, n);
    uint8_t z = 0;
    ne_write(e, buf + n, &z, 1);
    ret16(e, (uint16_t)n);
}

/* ---- DOS ---------------------------------------------------------------- */

static void s_DOS3Call(nemu *e, uint32_t sp) {
    (void)sp;
    uint32_t ax = 0, eflags = 0;
    uc_reg_read(e->uc, UC_X86_REG_AX, &ax);
    uc_reg_read(e->uc, UC_X86_REG_EFLAGS, &eflags);
    int ah = (ax >> 8) & 0xff;
    switch (ah) {
    case 0x30:                        /* get DOS version */
        e->ax = 0x1606;
        eflags &= ~1u;
        break;
    case 0x19:                        /* current drive */
        e->ax = 2;
        eflags &= ~1u;
        break;
    default:
        if (e->verbose)
            fprintf(stderr, "ne: DOS3Call ah=%02x refused\n", ah);
        e->ax = 0x0001;               /* invalid function */
        eflags |= 1u;                 /* carry: the call failed */
        break;
    }
    uc_reg_write(e->uc, UC_X86_REG_EFLAGS, &eflags);
}

/* ---- the table ---------------------------------------------------------- */

#define K(o, n, w, f) { "KERNEL", o, n, w, 0, 0, f }
#define KC(o, n, v)   { "KERNEL", o, n, 0, 1, v, NULL }

const ne_import ne_import_table[] = {
    K(1,   "FatalExit",              1,  s_FatalExit),
    K(2,   "ExitKernel",             0,  s_ExitKernel),
    K(3,   "GetVersion",             0,  s_GetVersion),
    K(4,   "LocalInit",              3,  s_LocalInit),
    K(15,  "GlobalAlloc",            3,  s_GlobalAlloc),
    K(16,  "GlobalReAlloc",          4,  s_GlobalReAlloc),
    K(17,  "GlobalFree",             1,  s_GlobalFree),
    K(18,  "GlobalLock",             1,  s_GlobalLock),
    K(19,  "GlobalUnlock",           1,  s_GlobalUnlock),
    K(20,  "GlobalSize",             1,  s_GlobalSize),
    K(21,  "GlobalHandle",           1,  s_GlobalHandle),
    K(23,  "LockSegment",            1,  s_LockSegment),
    K(24,  "UnlockSegment",          1,  s_UnlockSegment),
    K(47,  "GetModuleHandle",        2,  s_GetModuleHandle),
    K(48,  "GetModuleUsage",         1,  s_GetModuleUsage),
    K(49,  "GetModuleFileName",      4,  s_GetModuleFileName),
    K(50,  "GetProcAddress",         3,  s_GetProcAddress),
    K(51,  "MakeProcInstance",       3,  s_MakeProcInstance),
    K(52,  "FreeProcInstance",       2,  s_FreeProcInstance),
    K(91,  "InitTask",               0,  s_InitTask),
    K(95,  "LoadLibrary",            2,  s_LoadLibrary),
    K(96,  "FreeLibrary",            1,  s_FreeLibrary),
    K(102, "DOS3Call",               0,  s_DOS3Call),
    K(107, "SetErrorMode",           1,  s_SetErrorMode),
    KC(113, "__AHSHIFT",             3),
    KC(114, "__AHINCR",              8),
    K(128, "GetPrivateProfileString", 11, s_GetPrivateProfileString),
    K(131, "GetDOSEnvironment",      0,  s_GetDOSEnvironment),
    K(137, "FatalAppExit",           3,  s_FatalAppExit),
    K(171, "AllocDStoCSAlias",       1,  s_AllocDStoCSAlias),
    K(176, "FreeSelector",           1,  s_FreeSelector),
    KC(178, "__WINFLAGS",            0x413),
    K(184, "GlobalDOSAlloc",         2,  s_GlobalDOSAlloc),
    K(185, "GlobalDOSFree",          1,  s_GlobalDOSFree),
    K(190, "GetWinFlags",            0,  s_GetWinFlags),
};

const int ne_import_table_len = (int)(sizeof ne_import_table / sizeof ne_import_table[0]);

const ne_import *ne_import_find(const char *mod, int ordinal, const char *name) {
    for (int i = 0; i < ne_import_table_len; i++) {
        const ne_import *p = &ne_import_table[i];
        if (strcasecmp(p->mod, mod) != 0) continue;
        if (ordinal >= 0 && p->ordinal == ordinal) return p;
        if (name && strcasecmp(p->name, name) == 0) return p;
    }
    return NULL;
}
