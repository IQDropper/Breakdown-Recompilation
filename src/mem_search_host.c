/*
 * mem_search_host.c - a live memory search over the game's RAM, for finding where it keeps things
 * (the player's life, position, speed...). A developer tool, off unless RECOMP_MEMSEARCH names a command
 * file (relative to the working folder; "1" means memsearch.txt).
 *
 * A thread reads new lines appended to the command file (every 100 ms) and logs results to stderr as
 * "[MEMSEARCH] ...". Commands (one per line):
 *   snap f|i            start a search: every 4-byte-aligned float (f) or int32 (i) in guest RAM is a candidate
 *   changed | same      keep candidates whose value changed / stayed since the last step
 *   inc | dec           keep candidates that went up / down
 *   range LO HI         keep candidates with LO <= value <= HI
 *   list [N]            print up to N candidates (default 40) with their values
 *   dump ADDR [N]       print N dwords (default 64) from ADDR as hex, int and float
 *   poke ADDR i|f VAL   write one value (to test a guess)
 *   note TEXT           echo TEXT into the log (marks a step)
 * Addresses are guest (Xbox) addresses in hex; *ADDR[+OFS] follows the pointer stored at
 * ADDR; A>B>C follows a chain (2A92D0>48>10 = [[2A92D0]+48]+10). Reads race the game thread; that is fine for searching.
 */
#include "mem_search_host.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

extern ptrdiff_t g_xbox_mem_offset;

#define LO_VA 0x00010000u
#define HI_VA 0x08000000u   /* 128 MB of guest space; uncommitted pages are skipped */

struct region {
    uint32_t va, n;      /* n dwords */
    uint32_t *prev;
    uint8_t *keep;       /* 1 = still a candidate */
};

static struct region s_reg[512];
static int s_nreg, s_float;
static size_t s_count;

static volatile uint32_t *gp(uint32_t va) { return (volatile uint32_t *)((uintptr_t)va + g_xbox_mem_offset); }

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[MEMSEARCH] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static float as_f(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

static void clear(void)
{
    int i;
    for (i = 0; i < s_nreg; i++) {
        free(s_reg[i].prev);
        free(s_reg[i].keep);
    }
    s_nreg = 0;
    s_count = 0;
}

static void snap(int is_float)
{
    uint32_t va = LO_VA;
    clear();
    s_float = is_float;
    while (va < HI_VA && s_nreg < (int)(sizeof s_reg / sizeof s_reg[0])) {
        MEMORY_BASIC_INFORMATION mi;
        uintptr_t host = (uintptr_t)va + g_xbox_mem_offset, end;
        if (!VirtualQuery((void *)host, &mi, sizeof mi))
            break;
        end = (uintptr_t)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mi.Protect & PAGE_GUARD)) {
            uint32_t a = va, b = (uint32_t)(end - g_xbox_mem_offset), i;
            struct region *r = &s_reg[s_nreg];
            if (b > HI_VA)
                b = HI_VA;
            r->va = a;
            r->n = (b - a) / 4;
            r->prev = (uint32_t *)malloc((size_t)r->n * 4);
            r->keep = (uint8_t *)malloc(r->n);
            if (!r->prev || !r->keep) {
                out("out of memory at %08X", a);
                break;
            }
            for (i = 0; i < r->n; i++) {
                uint32_t v = *gp(a + i * 4);
                r->prev[i] = v;
                r->keep[i] = is_float ? (isfinite(as_f(v)) && v != 0) : 1;
                s_count += r->keep[i];
            }
            s_nreg++;
        }
        va = (uint32_t)(end - g_xbox_mem_offset);
    }
    out("snap %s: %zu candidates in %d regions", is_float ? "float" : "int", s_count, s_nreg);
}

/* op: 0 changed, 1 same, 2 inc, 3 dec, 4 range */
static void filter(int op, double lo, double hi)
{
    int k;
    s_count = 0;
    for (k = 0; k < s_nreg; k++) {
        struct region *r = &s_reg[k];
        uint32_t i;
        for (i = 0; i < r->n; i++) {
            uint32_t v, p;
            int ok;
            double dv, dp;
            if (!r->keep[i])
                continue;
            v = *gp(r->va + i * 4);
            p = r->prev[i];
            dv = s_float ? (double)as_f(v) : (double)(int32_t)v;
            dp = s_float ? (double)as_f(p) : (double)(int32_t)p;
            if (s_float && !isfinite(dv))
                ok = 0;
            else if (op == 0)
                ok = v != p;
            else if (op == 1)
                ok = v == p;
            else if (op == 2)
                ok = dv > dp;
            else if (op == 3)
                ok = dv < dp;
            else
                ok = dv >= lo && dv <= hi;
            r->keep[i] = (uint8_t)ok;
            r->prev[i] = v;
            s_count += ok;
        }
    }
    out("%zu candidates left", s_count);
}

static void list(int n)
{
    int k;
    for (k = 0; k < s_nreg && n > 0; k++) {
        struct region *r = &s_reg[k];
        uint32_t i;
        for (i = 0; i < r->n && n > 0; i++) {
            uint32_t v;
            if (!r->keep[i])
                continue;
            v = *gp(r->va + i * 4);
            out("  %08X = %08X  i=%d  f=%g", r->va + i * 4, v, (int32_t)v, as_f(v));
            n--;
        }
    }
}

static void dump(uint32_t va, int n)
{
    int i;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)((uintptr_t)va + g_xbox_mem_offset), &mi, sizeof mi) || mi.State != MEM_COMMIT) {
        out("dump %08X: not mapped", va);
        return;
    }
    va &= ~3u;
    for (i = 0; i < n; i++) {
        uint32_t v = *gp(va + i * 4);
        out("  %08X +%03X: %08X  i=%d  f=%g", va + i * 4, i * 4, v, (int32_t)v, as_f(v));
    }
}

static struct { char name[24]; volatile uint32_t va; } s_names[16];

void mem_search_name(const char *name, uint32_t va)
{
    int i;
    for (i = 0; i < 16; i++)
        if (!s_names[i].name[0] || !strcmp(s_names[i].name, name)) {
            if (!s_names[i].name[0])
                snprintf(s_names[i].name, sizeof s_names[i].name, "%s", name);
            s_names[i].va = va;
            return;
        }
}

/* "$name" (an address the game code published), "1234" or "*1234" (the dword stored at 1234) or "*1234+40" (that pointer plus 0x40) */
static uint32_t addr_of(const char *t)
{
    char *end;
    uint32_t v;
    if (*t == '$') {
        int i;
        for (i = 0; i < 16; i++)
            if (s_names[i].name[0] && !strcmp(s_names[i].name, t + 1))
                return s_names[i].va;
        out("no address named %s yet", t + 1);
        return 0;
    }
    if (strchr(t, '>')) {   /* A>B>C: start at A, then follow the pointer and add B, then again and add C */
        v = (uint32_t)strtoul(t, &end, 16);
        while (*end == '>')
            v = *gp(v) + (uint32_t)strtoul(end + 1, &end, 16);
        return v;
    }
    if (*t != '*')
        return (uint32_t)strtoul(t, NULL, 16);
    v = *gp((uint32_t)strtoul(t + 1, &end, 16));
    if (*end == '+')
        v += (uint32_t)strtoul(end + 1, NULL, 16);
    return v;
}

static void run(char *line)
{
    char a[32] = "", b[32] = "", c[32] = "";
    char *nl = strpbrk(line, "\r\n");
    if (nl)
        *nl = 0;
    if (!*line)
        return;
    out("> %s", line);
    sscanf(line, "%*s %31s %31s %31s", a, b, c);
    if (!strncmp(line, "snap", 4))
        snap(a[0] != 'i');
    else if (!strcmp(line, "changed"))
        filter(0, 0, 0);
    else if (!strcmp(line, "same"))
        filter(1, 0, 0);
    else if (!strcmp(line, "inc"))
        filter(2, 0, 0);
    else if (!strcmp(line, "dec"))
        filter(3, 0, 0);
    else if (!strncmp(line, "range", 5))
        filter(4, atof(a), atof(b));
    else if (!strncmp(line, "list", 4))
        list(a[0] ? atoi(a) : 40);
    else if (!strncmp(line, "dump", 4))
        dump(addr_of(a), b[0] ? atoi(b) : 64);
    else if (!strncmp(line, "poke", 4)) {
        uint32_t va = addr_of(a), v;
        if (b[0] == 'f') {
            float f = (float)atof(c);
            memcpy(&v, &f, 4);
        } else
            v = (uint32_t)strtol(c, NULL, 0);
        if (!va) {
            out("poke: no address");
            return;
        }
        *gp(va) = v;
        out("poked %08X = %08X", va, v);
    } else if (strncmp(line, "note", 4))
        out("unknown command");
}

static DWORD WINAPI ms_thread(LPVOID arg)
{
    const char *path = (const char *)arg;
    long done = 0;
    FILE *f = fopen(path, "w");   /* start with an empty command file */
    if (f)
        fclose(f);
    out("ready; commands from %s", path);
    for (;;) {
        Sleep(100);
        if (!g_xbox_mem_offset)
            continue;
        f = fopen(path, "rb");
        if (!f)
            continue;
        if (fseek(f, done, SEEK_SET) == 0) {
            char line[256];
            long at = done;
            while (fgets(line, sizeof line, f)) {
                if (!strchr(line, '\n'))
                    break;   /* wait for the whole line */
                at = ftell(f);
                run(line);
            }
            done = at;
        }
        fclose(f);
    }
    return 0;
}

void mem_search_start(void)
{
    static int started;
    static char path[MAX_PATH];
    const char *e = getenv("RECOMP_MEMSEARCH");
    HANDLE t;
    if (started || !e || !*e || !strcmp(e, "0"))
        return;
    started = 1;
    snprintf(path, sizeof path, "%s", strcmp(e, "1") ? e : "memsearch.txt");
    t = CreateThread(NULL, 0, ms_thread, path, 0, NULL);
    if (t)
        CloseHandle(t);
}
