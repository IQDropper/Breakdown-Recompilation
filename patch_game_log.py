"""patch_game_log.py - idempotent. Override the retail game's empty debug printf (0x161680, just `ret`)
so its own messages ("makino: ...") reach stderr as [GAME] lines. Opt-in: RECOMP_GAME_LOG=1.
Why: the strings are still passed in retail builds; hooking the stub gives the game's own trace for free."""
import io
P = r'src\recomp_manual.c'
MARK = '/* [game-log] */'
src = io.open(P, encoding='utf-8', newline='').read()
if MARK in src:
    print('already patched'); raise SystemExit
code = MARK + r'''
/* cdecl void DebugPrintf(const char *fmt, ...) - retail stub (ret). Minimal guest-side formatter. */
static int s_game_log = -1;
static void glog_str(char *o, size_t *n, size_t cap, uint32_t p)
{
    size_t k;
    if (!p) { const char *z = "(null)"; while (*z && *n < cap) o[(*n)++] = *z++; return; }
    for (k = 0; k < 200 && *n < cap; k++) { char ch = (char)MEM8(p + k); if (!ch) break; o[(*n)++] = ch; }
}
void sub_00161680(void)
{
    char out[512], spec[16]; size_t n = 0; uint32_t fmt, ap; int k;
    if (s_game_log < 0) { const char *e = getenv("RECOMP_GAME_LOG"); s_game_log = e && *e == '1'; }
    if (s_game_log) {
        fmt = MEM32(g_esp + 4); ap = g_esp + 8;
        for (k = 0; k < 300 && n < sizeof out - 40; k++) {
            char c = (char)MEM8(fmt + k), t; int si = 0;
            if (!c) break;
            if (c != '%') { out[n++] = c; continue; }
            spec[si++] = '%';
            while (si < 12) { t = (char)MEM8(fmt + ++k); if (!t) break;
                if (t == 'l' || t == 'h') continue;
                spec[si++] = t; if (strchr("diuxXcsp%f", t)) break; }
            spec[si] = 0; t = spec[si - 1];
            if (t == '%') { out[n++] = '%'; continue; }
            if (t == 's') { glog_str(out, &n, sizeof out - 40, MEM32(ap)); ap += 4; continue; }
            if (t == 'f') { double d; uint32_t w[2] = { MEM32(ap), MEM32(ap + 4) }; memcpy(&d, w, 8); ap += 8;
                n += (size_t)snprintf(out + n, sizeof out - n, spec, d); continue; }
            n += (size_t)snprintf(out + n, sizeof out - n, spec, MEM32(ap)); ap += 4;
            if (n >= sizeof out) n = sizeof out - 1;
        }
        while (n && (out[n-1] == '\n' || out[n-1] == '\r')) n--;
        out[n] = 0;
        fprintf(stderr, "[GAME] %s  (from 0x%08X)\n", out, MEM32(g_esp)); fflush(stderr);
    }
    g_esp += 4;   /* cdecl: pop return address only */
}
'''
if '#include <string.h>' not in src:
    src = src.replace('#include <stdlib.h>', '#include <stdlib.h>\n#include <string.h>', 1)
src = src.rstrip('\r\n') + '\n\n' + code
io.open(P, 'w', encoding='utf-8', newline='').write(src)
print('patched')
