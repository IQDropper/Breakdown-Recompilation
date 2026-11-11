/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include "recomp_env.h"
#include "pc_options_host.h"
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include "gen/recomp_types.h"   /* g_esp/g_eax (thread-local) + MEM32 - for the input bridge */

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

/* g_eax: declared thread-local by recomp_types.h (a plain extern here would name the wrong copy) */
extern ptrdiff_t g_xbox_mem_offset;

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu) site-ret=0x%08X stack: %08X %08X %08X %08X\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count, MEM32(g_esp), MEM32(g_esp+4), MEM32(g_esp+8), MEM32(g_esp+12), MEM32(g_esp+16));
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>
#include <string.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = recomp_env(RENV_UNIMPL_TRAP);
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_DEBUG=unimpl_trap to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* == Input bridge (patch_input_bridge.py) == */
/* Breakdown's XAPI gamepad calls -> the toolkit's host input (xbox_Input*). All are __stdcall: on entry
 * MEM32(g_esp) is the return address, args follow; the callee pops them (g_esp += 4 + 4*nargs). */
typedef struct { uint32_t dwPacketNumber; uint16_t wButtons; uint8_t bAnalog[8]; int16_t thumb[4]; } HostPadState;
typedef struct { uint16_t left, right; } HostVibration;
uint32_t xbox_InputGetState(uint32_t port, HostPadState *st);
uint32_t xbox_InputSetState(uint32_t port, const HostVibration *v);
int      xbox_InputIsConnected(uint32_t port);
#define PAD_HANDLE(port) (0xFEED0000u | (port))
static uint32_t s_pads_known;

/* BOOL XGetDeviceChanges(PXPP_DEVICE_TYPE type, PDWORD inserted, PDWORD removed). Only answer the gamepad poll
 * (called from Input_PollGamepads 0x18c5a0..0x18c830); anything else (memory units) sees no changes. */
void sub_00204CDD(void)
{
    uint32_t ret = MEM32(g_esp), pins = MEM32(g_esp + 8), prem = MEM32(g_esp + 12), now = 0, ins = 0, rem = 0, p;
    if (ret >= 0x0018C5A0u && ret < 0x0018C830u) {
        for (p = 0; p < 4; p++) if (xbox_InputIsConnected(p)) now |= 1u << p;
        /* RECOMP_KEYBOARD=1 makes port 0 answer XInputGetState with no pad plugged in (xbox_InputGetState merges
         * keys and mouse there), but xbox_InputIsConnected only counts real pads. Without this the game never
         * sees a pad inserted, never opens one, and a keyboard-only player cannot even skip the intro. */
        if (recomp_env_on(RENV_KEYBOARD)) now |= 1u;
        ins = now & ~s_pads_known; rem = s_pads_known & ~now; s_pads_known = now;
        if (ins | rem) { fprintf(stderr, "[INPUT] pads now 0x%X (inserted 0x%X removed 0x%X)\n", now, ins, rem); fflush(stderr); }
    }
    if (pins) MEM32(pins) = ins;
    if (prem) MEM32(prem) = rem;
    g_eax = (ins | rem) != 0; g_esp += 16;
}
/* HANDLE XInputOpen(type, DWORD port, DWORD slot, pollingParams) */
void sub_002051FF(void) { g_eax = PAD_HANDLE(MEM32(g_esp + 8) & 3u); g_esp += 20; }
/* VOID XInputClose(HANDLE) */
void sub_00205255(void) { g_esp += 8; }
/* DWORD XInputGetCapabilities(HANDLE, PXINPUT_CAPABILITIES): first byte = SubType, 1 = standard gamepad */
void sub_00205261(void)
{
    uint32_t caps = MEM32(g_esp + 8), i;
    for (i = 0; i < 0x16; i++) MEM8(caps + i) = 0;
    MEM8(caps) = 1; g_eax = 0; g_esp += 12;
}
/* DWORD XInputGetState(HANDLE, PXINPUT_STATE): 4-byte packet number + 18-byte XINPUT_GAMEPAD (same layout) */
void sub_00205439(void)
{
    uint32_t h = MEM32(g_esp + 4), dst = MEM32(g_esp + 8), i;
    uint32_t port = h & 3u;
    HostPadState st;
    const uint8_t *b;
    struct pc_pad_result pr;
    if (xbox_InputGetState(port, &st) != 0) {
        /* A disconnected pad must not leave Start or the d-pad held. */
        pc_options_host_on_port((int)port, 0);
        g_eax = 1167; g_esp += 12; return;   /* ERROR_DEVICE_NOT_CONNECTED */
    }
    /* PC options is a host page. Start alone still reaches the guest. */
    pr = pc_options_host_on_port((int)port, st.wButtons);
    st.wButtons = (uint16_t)pr.guest_buttons;
    if (pr.suppress_motion) {
        memset(st.bAnalog, 0, sizeof st.bAnalog);
        memset(st.thumb, 0, sizeof st.thumb);
    }
    b = (const uint8_t *)&st;
    for (i = 0; i < 22; i++) MEM8(dst + i) = b[i];
    g_eax = 0; g_esp += 12;
}
/* DWORD XInputSetState(HANDLE, PXINPUT_FEEDBACK): header.dwStatus at +0, rumble motors at +0x42/+0x44.
 * Completes at once (status 0); the game re-sends while status != ERROR_IO_PENDING, which is harmless. */
void sub_002054AC(void)
{
    uint32_t h = MEM32(g_esp + 4), fb = MEM32(g_esp + 8);
    HostVibration v;
    v.left = MEM16(fb + 0x42); v.right = MEM16(fb + 0x44);
    xbox_InputSetState(h & 3u, &v);
    MEM32(fb) = 0; g_eax = 0; g_esp += 12;
}

/* [game-log] */
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

/* [fps60] */
/* cdecl void SetFrameInterval(int vblanks) @ 0x21C50: clamps to 1..4 and stores it at 0x434F60. Render_SubmitFrame
 * (0x18EA30) then spins until the vblank counter 0x434F90 (++ in the vblank callback 0x18C880) reaches it, and resets
 * the counter. Menus pass 1 (60 FPS); entering gameplay (0x1BE70) passes 2, the 30 FPS cap (S8, seen running with
 * RECOMP_PEEK: 1 in menus, 2 in play). Game_Main (0x113B0) steps its logic by measured time
 * (dt = 1/60 x max(elapsed, one frame) / one frame, capped); seen running: dt 0.0167 at 60 FPS.
 *
 * RECOMP_FPS60=1: in play, one vblank per frame, but only while the game is not reading files. Forcing 1 for the
 * whole time made the save load take ~80 s instead of 1.3 s (S8): with the single emulated guest CPU the loader
 * thread mostly runs while the main thread idles in that vblank spin (inferred). So the game's own interval stays
 * in force from SetFrameInterval until ReadFile (0x1AAF3E) has been quiet for FPS60_QUIET_MS, and comes back while
 * reads resume (streaming). Unset or 0 keeps the original behaviour exactly. */
#define FPS60_QUIET_MS 500u
extern unsigned long __stdcall GetTickCount(void);
extern void sub_001AAF3E_gen(void);
extern void sub_0018C880_gen(void);
static int s_fps60 = -1;
static volatile uint32_t s_fps60_game_iv;      /* what the game last asked for (1..4) */
static volatile uint32_t s_fps60_last_read;    /* GetTickCount() at the last ReadFile */
static volatile uint32_t s_fps60_reads;        /* ReadFile calls since the last report */
static uint32_t s_fps60_vblanks;

static int fps60_on(void)
{
    if (s_fps60 < 0) {
        const char *e = getenv("RECOMP_FPS60");
        s_fps60 = e && *e == '1';
        if (s_fps60) {
            fprintf(stderr, "[FPS60] on: 1 vblank per frame in play once file reads are quiet for %u ms\n", FPS60_QUIET_MS);
            fflush(stderr);
        }
    }
    return s_fps60;
}

void sub_00021C50(void)
{
    int32_t want = (int32_t)MEM32(g_esp + 4);
    uint32_t v = want <= 0 ? 1u : want >= 4 ? 4u : (uint32_t)want;   /* the original clamp */
    /* Recorded even when off, so the PC options page can switch 60 FPS on mid-game. */
    s_fps60_game_iv = v;
    s_fps60_last_read = GetTickCount();   /* start quiet-timing from here: the load follows this call */
    if (fps60_on()) {
        fprintf(stderr, "[FPS60] SetFrameInterval(%d) from 0x%08X; kept until reads go quiet\n", (int)want, MEM32(g_esp));
        fflush(stderr);
    }
    MEM32(0x434F60) = v;
    g_eax = v;
    g_esp += 4;   /* cdecl: pop return address only */
}

/* BOOL ReadFile(...) @ 0x1AAF3E (XAPI): note the time, then the original. */
void sub_001AAF3E(void)
{
    s_fps60_last_read = GetTickCount();
    s_fps60_reads++;
    sub_001AAF3E_gen();
}

/* The PC options page (src/pc_options_host.c): 60 FPS gameplay on or off now. Off puts the game's own
 * interval back at once; on lets the vblank callback below drop it to 1 once file reads are quiet. */
void breakdown_fps60_set(int on)
{
    int was;
    (void)fps60_on();   /* settle the startup value first */
    was = s_fps60;
    s_fps60 = on ? 1 : 0;
    if (was == s_fps60)
        return;
    if (!s_fps60 && s_fps60_game_iv > 1u && MEM32(0x434F60) != s_fps60_game_iv)
        MEM32(0x434F60) = s_fps60_game_iv;
    fprintf(stderr, "[FPS60] %s from the PC options page\n", s_fps60 ? "on" : "off");
    fflush(stderr);
}

/* The vblank callback @ 0x18C880 (++0x434F90): the original, then pick this frame interval. */
void sub_0018C880(void)
{
    sub_0018C880_gen();
    if (s_fps60 != 1 || s_fps60_game_iv <= 1u)
        return;
    {
        uint32_t quiet = GetTickCount() - s_fps60_last_read >= FPS60_QUIET_MS;
        uint32_t want = quiet ? 1u : s_fps60_game_iv, cur = MEM32(0x434F60);
        if (cur != want && (cur == 1u || cur == s_fps60_game_iv)) {
            MEM32(0x434F60) = want;
            fprintf(stderr, "[FPS60] interval %u -> %u (%s)\n", cur, want, quiet ? "reads quiet" : "reading");
            fflush(stderr);
        }
    }
    if (++s_fps60_vblanks % 600u == 0u) {
        fprintf(stderr, "[FPS60] last 10 s: %u ReadFile calls, interval %u\n", s_fps60_reads, MEM32(0x434F60));
        fflush(stderr);
        s_fps60_reads = 0;
    }
}

/* [pb-pipeline] */
/* D3D_BlockOnTime @ 0x1C0B10 (XDK D3D, recon name): wait until the GPU's completed "time" (read through
 * device+0x30) reaches a target. Under 0x2000 bytes outstanding it writes two NOPs, kicks, and spins on that
 * memory (loc_001C0C47). At 0x2000 or more it puts a NOP-with-notify in the pushbuffer and sleeps on an event
 * only a GPU interrupt signals (loc_001C0C58, KeWaitForSingleObject via 0x20BC18). Nothing raises that
 * interrupt here, which is why the toolkit keeps the title within one segment of the walker (its KickOff
 * flush wait) and the game waits ~15 ms a frame in busy rooms (S8).
 * RECOMP_PB_PIPELINE=1 (with the toolkit's patch_pb_pipeline.py, which acks KickOff at once) lets the title run
 * ahead, so this wait must poll in both cases: the body below is the generated one, verbatim, except for the one
 * jump marked [pb-pipeline]. Unset or 0 keeps the generated behaviour exactly. */
#include "gen/recomp_funcs.h"
static int s_pb_pipeline = -1;
static int pb_pipeline_on(void)
{
    if (s_pb_pipeline < 0) {
        const char *e = getenv("RECOMP_PB_PIPELINE");
        s_pb_pipeline = e && *e == '1';
        if (s_pb_pipeline) { fprintf(stderr, "[PB-PIPELINE] on: D3D_BlockOnTime polls instead of waiting on the GPU event\n"); fflush(stderr); }
    }
    return s_pb_pipeline;
}
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
void sub_001C0B10(void)
{
    uint32_t ebp = 0;
    ebp = g_ebp;  /* frameless: caller's frame */
    int _flags = 0; /* fallback flag var */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;
    ebp = g_seh_ebp; /* fpo_leaf: inherit caller's frame */

loc_001C0B10: ;
    PUSH32(esp, esi);
    esi = MEM32(0x1CE378);
    eax = MEM32(esi + 0x30);
    ecx = MEM32(eax);
    eax = MEM32(esi + 0x2C);
    edx = eax;
    _fb = (uint32_t)(ecx) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    edx = edx - ecx;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    PUSH32(esp, edi);
    edi = MEM32(esp + 0xC);
    ecx = eax;
    _fb = (uint32_t)(edi) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    ecx = ecx - edi;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(edx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ecx, edx (32-bit) */
    if (CMP_AE(_fa, _fb)) goto loc_001C0C53; /* jae: above or equal (unsigned >=) */

loc_001C0B34: ;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp edi, eax (32-bit) */
    if (CMP_NE(_fa, _fb)) goto loc_001C0B3F; /* jne: not equal / not zero */

loc_001C0B38: ;
    PUSH32(esp, 0);
    PUSH32(esp, 0x001C0B3Fu); RECOMP_ABI_CALL(0x001C0A50u, sub_001C0A50); /* call 0x001C0A50 */

loc_001C0B3F: ;
    PUSH32(esp, ebp);
    eax = edi;
    PUSH32(esp, 0x001C0B47u); RECOMP_ABI_CALL(0x001C0660u, sub_001C0660); /* call 0x001C0660 */

loc_001C0B47: ;
    ebp = eax;
    _fa = (uint32_t)(ebp) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ebp, 0 (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_001C0C52; /* je: equal / zero */

loc_001C0B51: ;
    PUSH32(esp, 0);
    PUSH32(esp, esi);
    PUSH32(esp, 0x001C0B59u); RECOMP_ABI_CALL(0x001C07E0u, sub_001C07E0); /* call 0x001C07E0 */

loc_001C0B59: ;
    edx = MEM32(esi);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(edx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, edx (32-bit) */
    if (CMP_A(_fa, _fb)) goto loc_001C0B62; /* ja: above (unsigned >) */

loc_001C0B5F: ;
    _fb = (uint32_t)(MEM32(esi + 0x44)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    eax = eax + MEM32(esi + 0x44);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */

loc_001C0B62: ;
    ecx = MEM32(ebp + 4);
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(edx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ecx, edx (32-bit) */
    if (CMP_A(_fa, _fb)) goto loc_001C0B6C; /* ja: above (unsigned >) */

loc_001C0B69: ;
    _fb = (uint32_t)(MEM32(esi + 0x44)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    ecx = ecx + MEM32(esi + 0x44);
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */

loc_001C0B6C: ;
    _fb = (uint32_t)(eax) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    ecx = ecx - eax;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    PUSH32(esp, ebx);
    ebx = MEM32(esp + 0x18);
    if ((_fas < 0)) goto loc_001C0C1B; /* js: sign (negative) */

loc_001C0B79: ;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(0x2400) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ecx, 0x2400 (32-bit) */
    if (CMP_B(_fa, _fb)) goto loc_001C0C1B; /* jb: below (unsigned <) */

loc_001C0B85: ;
    eax = MEM32(esi + 0x19FC);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_001C0BBA; /* je: equal / zero */

loc_001C0B8F: ;
    (void)0; /* emms - empty MMX state */
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, ebx);
    MEM32(esi + 0x1A00) = edi;
    { uint32_t _icall_target = MEM32(esi + 0x19FC); PUSH32(esp, 0x001C0B9Eu); RECOMP_ICALL_SAFE_AT_CC(_icall_target, _icall_esp, 0x001C0B98u); } /* indirect call */
    }

loc_001C0B9E: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    PUSH32(esp, 0);
    PUSH32(esp, esi);
    PUSH32(esp, 0x001C0BA9u); RECOMP_ABI_CALL(0x001C07E0u, sub_001C07E0); /* call 0x001C07E0 */

loc_001C0BA9: ;
    PUSH32(esp, eax);
    edx = ebp;
    ecx = esi;
    PUSH32(esp, 0x001C0BB3u); RECOMP_ABI_CALL(0x001C04F0u, sub_001C04F0); /* call 0x001C04F0 */

loc_001C0BB3: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0x2000) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0x2000 (32-bit) */
    if (CMP_B(_fa, _fb)) goto loc_001C0C1B; /* jb: below (unsigned <) */

loc_001C0BBA: ;
    edi = MEM32(ebp + 4);
    eax = 0; /* xor self */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */
    _fa = (uint32_t)((LO8(ebx)) & (8)) & 0xFFu; _fb = (uint32_t)(0) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* test LO8(ebx), 8 (8-bit) */
    MEM32(esi + 0x1DD0) = eax;
    if (CMP_NE(_fa, _fb)) goto loc_001C0BD4; /* jne: not equal / not zero */

loc_001C0BCA: ;
    MEM32(edi + 0x10) = 0x40110;
    MEM32(edi + 0x14) = eax;

loc_001C0BD4: ;
    MEM32(edi + 0x18) = 0x40100;
    MEM32(edi + 0x1C) = 5;
    PUSH32(esp, 0x001C0BE7u); RECOMP_ABI_CALL(0x001C0860u, sub_001C0860); /* call 0x001C0860 */

loc_001C0BE7: ;
    PUSH32(esp, 1);
    PUSH32(esp, esi);
    PUSH32(esp, 0x001C0BEFu); RECOMP_ABI_CALL(0x001C07E0u, sub_001C07E0); /* call 0x001C07E0 */

loc_001C0BEF: ;
    PUSH32(esp, eax);
    edx = ebp;
    ecx = esi;
    PUSH32(esp, 0x001C0BF9u); RECOMP_ABI_CALL(0x001C04F0u, sub_001C04F0); /* call 0x001C04F0 */

loc_001C0BF9: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0x2000) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0x2000 (32-bit) */
    if (CMP_AE(_fa, _fb) && !pb_pipeline_on()) goto loc_001C0C58; /* [pb-pipeline] jae >= 0x2000 outstanding: notify + GPU-event wait, which nothing raises */

loc_001C0C00: ;
    eax = 0x40100;
    ecx = 0; /* xor self */
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */
    MEM32(edi + 0x10) = eax;
    MEM32(edi + 0x14) = ecx;
    MEM32(edi + 0x18) = eax;
    MEM32(edi + 0x1C) = ecx;
    PUSH32(esp, 0x001C0C18u); RECOMP_ABI_CALL(0x001C0860u, sub_001C0860); /* call 0x001C0860 */

loc_001C0C18: ;
    edi = MEM32(ebp);

loc_001C0C1B: ;
    eax = MEM32(esi + 0x19FC);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_001C0C3D; /* je: equal / zero */

loc_001C0C25: ;
    (void)0; /* emms - empty MMX state */
    ebx = ebx | 0x80000000u;
    _fa = (uint32_t)(ebx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, ebx);
    MEM32(esi + 0x1A00) = edi;
    { uint32_t _icall_target = MEM32(esi + 0x19FC); PUSH32(esp, 0x001C0C3Au); RECOMP_ICALL_SAFE_AT_CC(_icall_target, _icall_esp, 0x001C0C34u); } /* indirect call */
    }

loc_001C0C3A: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */

loc_001C0C3D: ;
    edx = MEM32(esi + 0x30);
    esi = MEM32(esi + 0x2C);
    eax = esi;
    _fb = (uint32_t)(edi) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    eax = eax - edi;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */

loc_001C0C47: ;
    ecx = MEM32(edx);
    edi = esi;
    _fb = (uint32_t)(ecx) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    edi = edi - ecx;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(edi) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, edi (32-bit) */
    if (CMP_B(_fa, _fb)) { RECOMP_BACK_EDGE(0x001C0C4Fu); goto loc_001C0C47; } /* jb: below (unsigned <) */

loc_001C0C51: ;
    POP32(esp, ebx);

loc_001C0C52: ;
    POP32(esp, ebp);

loc_001C0C53: ;
    POP32(esp, edi);
    POP32(esp, esi);
    esp += 12; return; /* ret 8 */

loc_001C0C58: ;
    edi = MEM32(0x20BC18);
    _fb = (uint32_t)(0x1DCC) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esi = esi + 0x1DCC;
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */

loc_001C0C64: ;
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, 0);
    PUSH32(esp, 0);
    PUSH32(esp, 1);
    PUSH32(esp, 6);
    PUSH32(esp, esi);
    { uint32_t _icall_target = edi; PUSH32(esp, 0x001C0C6Fu); RECOMP_ICALL_SAFE_AT(_icall_target, _icall_esp, 0x001C0C6Du); } /* indirect call */
    }

loc_001C0C6F: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    if (CMP_NE(_fa, _fb)) { RECOMP_BACK_EDGE(0x001C0C71u); goto loc_001C0C64; } /* jne: not equal / not zero */

loc_001C0C73: ;
    POP32(esp, ebx);
    POP32(esp, ebp);
    POP32(esp, edi);
    POP32(esp, esi);
    esp += 12; return; /* ret 8 */

}
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi

/* ---- VIDEO SETTINGS row in the main-menu Options list (research: kit reports/research/game-menus-notes.md) ----
 * The list handler @ 0x11C140 already has a 5th entry, a cut "SAVE SETTINGS" row: its scene objects, focus
 * animations (table 0x2708A0[4]) and A-press case (jump table 0x11C310[4] = "not allowed" sound) all exist; only
 * the up/down wrap (0..3) hides it. RECOMP_VIDEO_ROW=1: the wrap includes row 4, and A on it opens the PC options
 * page. Off (default) keeps the original exactly. */
extern void sub_0011C140_gen(void);
extern void pc_options_host_open(void);
static int s_video_row = -1;

static int video_row_on(void)
{
    if (s_video_row < 0) {
        const char *e = getenv("RECOMP_VIDEO_ROW");
        s_video_row = e && *e == '1';
        if (s_video_row) {
            fprintf(stderr, "[VIDEO-ROW] on: 5th row in the main-menu Options list opens the video settings\n");
            fflush(stderr);
        }
    }
    return s_video_row;
}

void sub_0011C140(void)
{
    uint8_t before, after;
    uint32_t f;
    if (!video_row_on()) {
        sub_0011C140_gen();
        return;
    }
    before = MEM8(0x3EA597);
    f = MEM32(0x3EA574);
    if (before == 4 && (((f >> 8) & 1u) || (f & 0x10u))) {   /* A (or Start) on our row */
        pc_options_host_open();
        g_eax = 0x11C140;   /* stay on this list */
        g_esp += 4;         /* pop the return address, as the original's ret */
        return;
    }
    sub_0011C140_gen();
    after = MEM8(0x3EA597);
    /* The original wraps 3 -> 0 (down) and 0 -> 3 (up); put row 4 in between. 4 -> 3 (up) and 4 -> 5 -> 0 (down)
     * already work. The returned state 0x11C330 then focuses the cursor's row. */
    if (g_eax == 0x11C330 && ((before == 3 && after == 0) || (before == 0 && after == 3)))
        MEM8(0x3EA597) = 4;
}
