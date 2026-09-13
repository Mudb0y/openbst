#include <stdlib.h>
#include <string.h>
#include "winemu.h"

#define A(i) emu_arg(e, esp, i)

/* ---- winmm: the whole point of the exercise ---------------------------- */

#define FAKE_HWAVEOUT 0xbe570001u
#define WHDR_DONE     0x00000001u
#define WHDR_PREPARED 0x00000002u

static uint32_t s_waveOutOpen(emu *e, uint32_t esp) {
    uint32_t phwo = A(0), fmt = A(2), fdwOpen = A(5);

    if (fmt) {
        uint16_t channels = 0, bits = 0;
        uint32_t rate = 0;
        emu_read(e, fmt + 2, &channels, 2);
        emu_read(e, fmt + 4, &rate, 4);
        emu_read(e, fmt + 14, &bits, 2);
        if (rate) e->sample_rate = rate;
        if (channels) e->channels = channels;
        if (bits) e->bits = bits;
    }
    if (fdwOpen & 0x00000001u) return 0;   /* WAVE_FORMAT_QUERY */
    if (phwo) emu_wr32(e, phwo, FAKE_HWAVEOUT);
    e->wave_open = 1;
    return 0;
}

static uint32_t s_waveOutPrepareHeader(emu *e, uint32_t esp) {
    uint32_t pwh = A(1);
    if (pwh) emu_wr32(e, pwh + 0x10, emu_rd32(e, pwh + 0x10) | WHDR_PREPARED);
    return 0;
}

static uint32_t s_waveOutWrite(emu *e, uint32_t esp) {
    uint32_t pwh = A(1);
    if (!pwh) return 0;
    uint32_t data = emu_rd32(e, pwh + 0x00);
    uint32_t len  = emu_rd32(e, pwh + 0x04);

    if (data && len) {
        if (e->pcm_len + len > e->pcm_cap) {
            size_t cap = e->pcm_cap ? e->pcm_cap * 2 : 1 << 16;
            while (cap < e->pcm_len + len) cap *= 2;
            uint8_t *p = realloc(e->pcm, cap);
            if (!p) return 8;   /* MMSYSERR_NOMEM */
            e->pcm = p;
            e->pcm_cap = cap;
        }
        if (emu_read(e, data, e->pcm + e->pcm_len, len) == 0)
            e->pcm_len += len;
    }
    /* Retire the buffer immediately: the engine polls WHDR_DONE rather than
       waiting on the device, so there is nothing to schedule. */
    emu_wr32(e, pwh + 0x10, emu_rd32(e, pwh + 0x10) | WHDR_DONE);
    return 0;
}

static uint32_t s_waveOutUnprepareHeader(emu *e, uint32_t esp) {
    uint32_t pwh = A(1);
    if (pwh) emu_wr32(e, pwh + 0x10, emu_rd32(e, pwh + 0x10) & ~WHDR_PREPARED);
    return 0;
}

static uint32_t s_waveOutReset(emu *e, uint32_t esp) { (void)e; (void)esp; return 0; }
static uint32_t s_waveOutClose(emu *e, uint32_t esp) { (void)esp; e->wave_open = 0; return 0; }

/* ---- kernel32: memory -------------------------------------------------- */

static uint32_t s_GlobalAlloc(emu *e, uint32_t esp)  { return emu_alloc(e, A(1)); }
static uint32_t s_GlobalLock(emu *e, uint32_t esp)   { (void)e; return A(0); }
static uint32_t s_GlobalUnlock(emu *e, uint32_t esp) { (void)e; (void)esp; return 1; }
static uint32_t s_GlobalFree(emu *e, uint32_t esp)   { (void)e; (void)esp; return 0; }
static uint32_t s_LocalAlloc(emu *e, uint32_t esp)   { return emu_alloc(e, A(1)); }
static uint32_t s_LocalLock(emu *e, uint32_t esp)    { (void)e; return A(0); }
static uint32_t s_LocalUnlock(emu *e, uint32_t esp)  { (void)e; (void)esp; return 1; }
static uint32_t s_LocalFree(emu *e, uint32_t esp)    { (void)e; (void)esp; return 0; }

static uint32_t s_VirtualAlloc(emu *e, uint32_t esp) {
    uint32_t want = A(0), size = A(1);
    if (want) return want;           /* caller picked an address; pretend it worked */
    return emu_alloc(e, size + 0x1000);
}
static uint32_t s_VirtualFree(emu *e, uint32_t esp) { (void)e; (void)esp; return 1; }

/* ---- kernel32: TLS and critical sections ------------------------------- */

static uint32_t s_TlsAlloc(emu *e, uint32_t esp) {
    (void)esp;
    for (int i = 0; i < MAX_TLS; i++)
        if (!e->tls_used[i]) { e->tls_used[i] = 1; e->tls[i] = 0; return (uint32_t)i; }
    return 0xffffffffu;
}
static uint32_t s_TlsSetValue(emu *e, uint32_t esp) {
    uint32_t i = A(0);
    if (i < MAX_TLS) e->tls[i] = A(1);
    return 1;
}
static uint32_t s_TlsGetValue(emu *e, uint32_t esp) {
    uint32_t i = A(0);
    return i < MAX_TLS ? e->tls[i] : 0;
}
static uint32_t s_TlsFree(emu *e, uint32_t esp) {
    uint32_t i = A(0);
    if (i < MAX_TLS) e->tls_used[i] = 0;
    return 1;
}
static uint32_t s_nop1(emu *e, uint32_t esp) { (void)e; (void)esp; return 1; }
static uint32_t s_nop0(emu *e, uint32_t esp) { (void)e; (void)esp; return 0; }

/* ---- kernel32: process and environment --------------------------------- */

static uint32_t s_GetVersion(emu *e, uint32_t esp) { (void)e; (void)esp; return 0xc0000a04u; }
static uint32_t s_GetACP(emu *e, uint32_t esp)     { (void)e; (void)esp; return 1252; }
static uint32_t s_GetOEMCP(emu *e, uint32_t esp)   { (void)e; (void)esp; return 437; }

static uint32_t s_GetCPInfo(emu *e, uint32_t esp) {
    uint32_t p = A(1);
    if (p) {
        emu_wr32(e, p, 1);                      /* MaxCharSize */
        uint8_t rest[18] = { '?', 0 };          /* DefaultChar + LeadByte */
        emu_write(e, p + 4, rest, sizeof rest);
    }
    return 1;
}

static uint32_t s_GetCommandLineA(emu *e, uint32_t esp) {
    (void)esp;
    return emu_push_str(e, "b32.exe");
}

static uint32_t s_GetEnvironmentStrings(emu *e, uint32_t esp) {
    (void)esp;
    return emu_push_bytes(e, "\0", 2);
}

static uint32_t s_GetModuleHandleA(emu *e, uint32_t esp) {
    (void)esp;
    return e->image_base;
}

static uint32_t s_GetModuleFileNameA(emu *e, uint32_t esp) {
    uint32_t buf = A(1), n = A(2);
    const char *path = "C:\\B32_TTS.DLL";
    uint32_t len = (uint32_t)strlen(path);
    if (!buf || n == 0) return 0;
    if (len + 1 > n) len = n - 1;
    emu_write(e, buf, path, len);
    uint8_t z = 0;
    emu_write(e, buf + len, &z, 1);
    return len;
}

static uint32_t s_GetStdHandle(emu *e, uint32_t esp) { (void)e; return 0x10 + (A(0) & 0xf); }
static uint32_t s_GetFileType(emu *e, uint32_t esp)  { (void)e; (void)esp; return 2; }  /* FILE_TYPE_CHAR */

static uint32_t s_GetStartupInfoA(emu *e, uint32_t esp) {
    uint32_t p = A(0);
    if (p) {
        uint8_t si[68];
        memset(si, 0, sizeof si);
        si[0] = 68;
        emu_write(e, p, si, sizeof si);
    }
    return 0;
}

static uint32_t s_WriteFile(emu *e, uint32_t esp) {
    uint32_t buf = A(1), n = A(2), written = A(3);
    if (e->verbose && buf && n && n < 4096) {
        char *tmp = malloc(n + 1);
        if (tmp && emu_read(e, buf, tmp, n) == 0) {
            tmp[n] = 0;
            fprintf(stderr, "[guest] %s", tmp);
        }
        free(tmp);
    }
    if (written) emu_wr32(e, written, n);
    return 1;
}

/* Pinned so that two runs of the same text produce the same samples. */
static uint32_t s_GetLocalTime(emu *e, uint32_t esp) {
    uint32_t p = A(0);
    if (p) {
        uint16_t st[8] = { 2000, 1, 6, 1, 12, 0, 0, 0 };
        emu_write(e, p, st, sizeof st);
    }
    return 0;
}

static uint32_t s_GetTimeZoneInformation(emu *e, uint32_t esp) {
    uint32_t p = A(0);
    if (p) {
        uint8_t tz[172];
        memset(tz, 0, sizeof tz);
        emu_write(e, p, tz, sizeof tz);
    }
    return 0;   /* TIME_ZONE_ID_UNKNOWN */
}

static uint32_t s_GetCurrentThreadId(emu *e, uint32_t esp) { (void)e; (void)esp; return 0x2000; }
static uint32_t s_GetLastError(emu *e, uint32_t esp)       { (void)esp; return e->last_error; }
static uint32_t s_GetProcAddress(emu *e, uint32_t esp)     { (void)e; (void)esp; return 0; }
static uint32_t s_SetFilePointer(emu *e, uint32_t esp)     { (void)e; (void)esp; return 0; }

static uint32_t s_ExitProcess(emu *e, uint32_t esp) {
    (void)esp;
    e->exited = 1;
    uc_emu_stop(e->uc);
    return 0;
}

/* ---- kernel32: code pages ---------------------------------------------- */

/* The engine's text path is byte-oriented; these exist for the CRT. A
   Latin-1 mapping is sufficient for every code page it asks about. */
static uint32_t s_MultiByteToWideChar(emu *e, uint32_t esp) {
    uint32_t src = A(2); int32_t slen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dlen = (int32_t)A(5);

    uint32_t n = 0;
    if (slen < 0) {
        uint8_t c;
        do { emu_read(e, src + n, &c, 1); n++; } while (c);
    } else n = (uint32_t)slen;

    if (dlen == 0) return n;
    uint32_t out = n < (uint32_t)dlen ? n : (uint32_t)dlen;
    for (uint32_t i = 0; i < out; i++) {
        uint8_t c = 0;
        emu_read(e, src + i, &c, 1);
        uint16_t w = c;
        emu_write(e, dst + 2 * i, &w, 2);
    }
    return out;
}

static uint32_t s_WideCharToMultiByte(emu *e, uint32_t esp) {
    uint32_t src = A(2); int32_t slen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dlen = (int32_t)A(5);

    uint32_t n = 0;
    if (slen < 0) {
        uint16_t w;
        do { emu_read(e, src + 2 * n, &w, 2); n++; } while (w);
    } else n = (uint32_t)slen;

    if (dlen == 0) return n;
    uint32_t out = n < (uint32_t)dlen ? n : (uint32_t)dlen;
    for (uint32_t i = 0; i < out; i++) {
        uint16_t w = 0;
        emu_read(e, src + 2 * i, &w, 2);
        uint8_t c = w < 256 ? (uint8_t)w : '?';
        emu_write(e, dst + i, &c, 1);
    }
    return out;
}

/* ---- user32 ------------------------------------------------------------ */

static uint32_t s_RegisterClassA(emu *e, uint32_t esp) {
    uint32_t wc = A(0);
    if (wc) e->wndproc = emu_rd32(e, wc + 4);
    return 0xc001;
}
static uint32_t s_CreateWindowExA(emu *e, uint32_t esp) { (void)e; (void)esp; return 0x00cd0001u; }
static uint32_t s_PeekMessageA(emu *e, uint32_t esp)    { (void)e; (void)esp; return 0; }

const shim_def shim_table[] = {
    { "WINMM.dll",    "waveOutOpen",             6, s_waveOutOpen },
    { "WINMM.dll",    "waveOutPrepareHeader",    3, s_waveOutPrepareHeader },
    { "WINMM.dll",    "waveOutWrite",            3, s_waveOutWrite },
    { "WINMM.dll",    "waveOutUnprepareHeader",  3, s_waveOutUnprepareHeader },
    { "WINMM.dll",    "waveOutReset",            1, s_waveOutReset },
    { "WINMM.dll",    "waveOutClose",            1, s_waveOutClose },

    { "KERNEL32.dll", "ExitProcess",             1, s_ExitProcess },
    { "KERNEL32.dll", "MultiByteToWideChar",     6, s_MultiByteToWideChar },
    { "KERNEL32.dll", "WideCharToMultiByte",     8, s_WideCharToMultiByte },
    { "KERNEL32.dll", "SetEnvironmentVariableA", 2, s_nop1 },
    { "KERNEL32.dll", "GetTimeZoneInformation",  1, s_GetTimeZoneInformation },
    { "KERNEL32.dll", "GetLocalTime",            1, s_GetLocalTime },
    { "KERNEL32.dll", "CloseHandle",             1, s_nop1 },
    { "KERNEL32.dll", "FlushFileBuffers",        1, s_nop1 },
    { "KERNEL32.dll", "LocalFree",               1, s_LocalFree },
    { "KERNEL32.dll", "LocalUnlock",             1, s_LocalUnlock },
    { "KERNEL32.dll", "LocalLock",               1, s_LocalLock },
    { "KERNEL32.dll", "LocalAlloc",              2, s_LocalAlloc },
    { "KERNEL32.dll", "GlobalFree",              1, s_GlobalFree },
    { "KERNEL32.dll", "GlobalLock",              1, s_GlobalLock },
    { "KERNEL32.dll", "GlobalAlloc",             2, s_GlobalAlloc },
    { "KERNEL32.dll", "GlobalUnlock",            1, s_GlobalUnlock },
    { "KERNEL32.dll", "TlsGetValue",             1, s_TlsGetValue },
    { "KERNEL32.dll", "TlsFree",                 1, s_TlsFree },
    { "KERNEL32.dll", "TlsSetValue",             2, s_TlsSetValue },
    { "KERNEL32.dll", "TlsAlloc",                0, s_TlsAlloc },
    { "KERNEL32.dll", "LeaveCriticalSection",    1, s_nop0 },
    { "KERNEL32.dll", "EnterCriticalSection",    1, s_nop0 },
    { "KERNEL32.dll", "DeleteCriticalSection",   1, s_nop0 },
    { "KERNEL32.dll", "InitializeCriticalSection", 1, s_nop0 },
    { "KERNEL32.dll", "SetFilePointer",          4, s_SetFilePointer },
    { "KERNEL32.dll", "GetEnvironmentStrings",   0, s_GetEnvironmentStrings },
    { "KERNEL32.dll", "GetCommandLineA",         0, s_GetCommandLineA },
    { "KERNEL32.dll", "GetVersion",              0, s_GetVersion },
    { "KERNEL32.dll", "GetProcAddress",          2, s_GetProcAddress },
    { "KERNEL32.dll", "GetModuleHandleA",        1, s_GetModuleHandleA },
    { "KERNEL32.dll", "SetStdHandle",            2, s_nop1 },
    { "KERNEL32.dll", "GetCurrentThreadId",      0, s_GetCurrentThreadId },
    { "KERNEL32.dll", "GetLastError",            0, s_GetLastError },
    { "KERNEL32.dll", "GetOEMCP",                0, s_GetOEMCP },
    { "KERNEL32.dll", "VirtualFree",             3, s_VirtualFree },
    { "KERNEL32.dll", "VirtualAlloc",            4, s_VirtualAlloc },
    { "KERNEL32.dll", "GetModuleFileNameA",      3, s_GetModuleFileNameA },
    { "KERNEL32.dll", "GetACP",                  0, s_GetACP },
    { "KERNEL32.dll", "GetCPInfo",               2, s_GetCPInfo },
    { "KERNEL32.dll", "GetStdHandle",            1, s_GetStdHandle },
    { "KERNEL32.dll", "GetFileType",             1, s_GetFileType },
    { "KERNEL32.dll", "GetStartupInfoA",         1, s_GetStartupInfoA },
    { "KERNEL32.dll", "WriteFile",               5, s_WriteFile },

    { "USER32.dll",   "DefWindowProcA",          4, s_nop0 },
    { "USER32.dll",   "RegisterClassA",          1, s_RegisterClassA },
    { "USER32.dll",   "CreateWindowExA",        12, s_CreateWindowExA },
    { "USER32.dll",   "MessageBeep",             1, s_nop1 },
    { "USER32.dll",   "PeekMessageA",            5, s_PeekMessageA },
    { "USER32.dll",   "TranslateMessage",        1, s_nop0 },
    { "USER32.dll",   "DispatchMessageA",        1, s_nop0 },
};

const int shim_table_len = (int)(sizeof shim_table / sizeof shim_table[0]);

const shim_def *shim_lookup(const char *dll, const char *name) {
    for (int i = 0; i < shim_table_len; i++)
        if (strcmp(shim_table[i].name, name) == 0 &&
            strcasecmp(shim_table[i].dll, dll) == 0)
            return &shim_table[i];
    return NULL;
}
