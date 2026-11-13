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
#include "tuning_host.h"     /* tuning_host_pad: the tuning window's controller */
#include "a11y_input.h"
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include "gen/recomp_types.h"   /* g_esp/g_eax (thread-local) + MEM32 - for the input bridge */

/* â”€â”€ ICALL trace ring buffer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ Register state (defined in xbox_memory_layout.c) â”€â”€â”€â”€â”€â”€â”€â”€ */

/* g_eax: declared thread-local by recomp_types.h (a plain extern here would name the wrong copy) */
extern ptrdiff_t g_xbox_mem_offset;

/* â”€â”€ Manual function overrides â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ ICALL failure logging â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

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

/* â”€â”€ Untranslated instructions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>
#include <string.h>
#include "debug_text_host.h"
#include "speedrun_host.h"
#include "qol.h"
#include "qol_host.h"

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
/* Hold to repeat / hold toggle (A11Y-2/3, src/a11y_input.c). The choice comes from RECOMP_A11Y_REPEAT /
 * RECOMP_A11Y_TOGGLE (display_host_init copies breakdown_video.cfg values into the environment) and from
 * the PC options page (breakdown_a11y_set, window thread; plain ints, read once per poll). */
extern unsigned long __stdcall GetTickCount(void);   /* no windows.h in this file (see [fps60]) */
static volatile int s_a11y_repeat = -1, s_a11y_toggle = -1;
static struct a11y_pad s_a11y_pad[4];
static void a11y_settle(void)
{
    if (s_a11y_repeat >= 0)
        return;
    s_a11y_repeat = a11y_repeat_from_token(getenv("RECOMP_A11Y_REPEAT"));
    s_a11y_toggle = a11y_toggle_from_token(getenv("RECOMP_A11Y_TOGGLE"));
    if (s_a11y_repeat || s_a11y_toggle) {
        fprintf(stderr, "[A11Y] hold to repeat: %s, hold toggle: %s\n",
                a11y_repeat_label(s_a11y_repeat), a11y_toggle_label(s_a11y_toggle));
        fflush(stderr);
    }
}
void breakdown_a11y_set(int repeat_choice, int toggle_choice)
{
    a11y_settle();
    if (repeat_choice == s_a11y_repeat && toggle_choice == s_a11y_toggle)
        return;
    s_a11y_repeat = repeat_choice;
    s_a11y_toggle = toggle_choice;
    fprintf(stderr, "[A11Y] hold to repeat: %s, hold toggle: %s (PC options page)\n",
            a11y_repeat_label(repeat_choice), a11y_toggle_label(toggle_choice));
    fflush(stderr);
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
        /* A disconnected pad must not leave Start or the d-pad held, or a trigger latched. */
        pc_options_host_on_port((int)port, 0);
        a11y_pad_reset(&s_a11y_pad[port]);
        g_eax = 1167; g_esp += 12; return;   /* ERROR_DEVICE_NOT_CONNECTED */
    }
    /* PC options is a host page. Start alone still reaches the guest. */
    pr = pc_options_host_on_port((int)port, st.wButtons);
    st.wButtons = (uint16_t)pr.guest_buttons;
    tuning_host_pad((int)port, &st.wButtons, st.bAnalog, pr.suppress_motion);   /* LS + RS: tuning window */
    a11y_settle();
    if (pr.suppress_motion) {
        /* Page open: the filter sees nothing held (repeat stops, a latch stays), the game sees nothing. */
        uint8_t none[A11Y_BTN_COUNT] = { 0 };
        a11y_pad_filter(&s_a11y_pad[port], 0, a11y_toggle_mask(s_a11y_toggle), none, GetTickCount());
        memset(st.bAnalog, 0, sizeof st.bAnalog);
        memset(st.thumb, 0, sizeof st.thumb);
    } else
        a11y_pad_filter(&s_a11y_pad[port], a11y_repeat_mask(s_a11y_repeat), a11y_toggle_mask(s_a11y_toggle),
                        st.bAnalog, GetTickCount());
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

/* Set when F9 switched the MENU task to the debug menu (sub_0001B9F0 below). */
static uint32_t s_dbgmenu_task;
/* [game-log] */
/* cdecl void DebugPrintf(const char *fmt, ...) - retail stub (ret). Minimal guest-side formatter.
 * RECOMP_GAME_LOG=1 writes each print to stderr. With the debug text overlay on (src/debug_text_host.c:
 * RECOMP_DEBUG_MENU=1 or RECOMP_DEBUG_TEXT=1) each print also lands on the screen at the game's cursor, and
 * Render_SubmitFrame's own print (from 0x18EA4E, once per frame) ends the frame. */
static int s_game_log = -1;
static void glog_str(char *o, size_t *n, size_t cap, uint32_t p)
{
    size_t k;
    if (!p) { const char *z = "(null)"; while (*z && *n < cap) o[(*n)++] = *z++; return; }
    for (k = 0; k < 200 && *n < cap; k++) { char ch = (char)MEM8(p + k); if (!ch) break; o[(*n)++] = ch; }
}
/* RECOMP_SKIP_INTRO=1: skip the Dolby logo (dolby.xmv) and the ~2 minute opening movie (opening_b*.xmv), each once,
 * the first time it plays. The Namco logo and the warning are unskippable in the game (mode 2) and are left alone.
 * The boot task (0x26760 -> 0x26630, step at task +0x38) plays namcologo, warning, dolby, then opening_b
 * (0x26500 -> movie player 0x11B30 with mode 0), and step 7 only waits for the player's "playing" bit
 * ([0x29C61C] +0x34 bit 0) to clear; common/base.pak has already finished loading at step 3. The movie update
 * (0x11F70) already skips on any button: 0x118F0 sees a press, 0x11880 starts a fade-out (+0xE4 = 1,
 * +0xD0 = 0, +0xD4 = +0xD8) unless the mode says unskippable (+0xCC bit 1) or under 0.5 s have played (+0xC8).
 * The fade then ends the movie through the normal end path. This makes the same three writes 0x11880 makes.
 * Notes: kit reports/research/intro-skip-notes.md. */
static void intro_skip_frame(void)
{
    static int s_done[2];   /* [0] dolby, [1] opening_b */
    int which;
    uint32_t mv, bits;
    char name[24];
    float played;
    size_t k;
    if (s_done[0] && s_done[1])
        return;
    mv = MEM32(0x29C61Cu);
    if (!mv)
        return;
    for (k = 0; k < sizeof name - 1; k++) {
        name[k] = (char)MEM8(mv + 0x98 + k);
        if (!name[k])
            break;
    }
    name[k] = 0;
    which = strcmp(name, "dolby") == 0 ? 0 : 1;
    if (s_done[which])
        return;
    bits = MEM32(mv + 0xC8);
    memcpy(&played, &bits, sizeof played);
    switch (qol_intro_decide(name, MEM32(mv + 0x34), MEM32(mv + 0xCC), played, MEM32(mv + 0xE4))) {
    case QOL_INTRO_SKIP:
        MEM32(mv + 0xE4) = 1;                  /* fading out */
        MEM32(mv + 0xD0) = 0;                  /* fade time so far */
        MEM32(mv + 0xD4) = MEM32(mv + 0xD8);   /* fade length (the game's own value) */
        s_done[which] = 1;
        fprintf(stderr, "[SKIP-INTRO] %s: fading out at %.1f s (RECOMP_SKIP_INTRO=1)\n", name, (double)played);
        fflush(stderr);
        qol_note("intro skipped");
        break;
    case QOL_INTRO_LEAVE:
        s_done[which] = 1;
        fprintf(stderr, "[SKIP-INTRO] %s already fading or unskippable: left alone\n", name);
        fflush(stderr);
        break;
    default:
        break;
    }
}

void sub_00161680(void)
{
    char out[512], spec[16]; size_t n = 0; uint32_t fmt, ap; int k, text;
    if (s_game_log < 0) { const char *e = getenv("RECOMP_GAME_LOG"); s_game_log = e && *e == '1'; }
    text = debug_text_on();
    if (s_dbgmenu_task && getenv("RECOMP_DEBUG_MENU_PROBE")) {
        static uint32_t last[4];
        uint32_t v[4] = { MEM32(0x2988F0), MEM32(0x2988F4), MEM32(0x2988F8), MEM32(0x29BEB0) };
        if (memcmp(v, last, sizeof v)) {
            memcpy(last, v, sizeof v);
            fprintf(stderr, "[DEBUG-MENU] pad F0=%08X F4=%08X F8=%08X BEB0=%08X\n", v[0], v[1], v[2], v[3]);
        }
    }
    if (text && MEM32(g_esp) == 0x0018EA4Eu)
        debug_text_frame_end();
    if (MEM32(g_esp) == 0x0018EA4Eu && speedrun_on())
        speedrun_frame(MEM32(0x29F6E0) == 1);   /* speedrun timer: the pause flag, 0 = playing, 1 = paused */
    if (MEM32(g_esp) == 0x0018EA4Eu) {
        qol_frame();   /* FPS overlay + F12 screenshot (src/qol_host.c) */
        if (qol_skip_intro_on())
            intro_skip_frame();
    }
    fmt = MEM32(g_esp + 4);
    /* Some task slots point at this stub (task +0x28/+0x2C, called from 0x219D0/0x17E389 with the task) and the
     * hero update passes its status block (0x3EA99): the "format" is then a heap struct, printed as garbage.
     * Real formats are literals in the XBE image. */
    if (fmt < 0x10000u || fmt >= 0x00400000u)
        text = 0;
    if (s_game_log || text) {
        ap = g_esp + 8;
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
        out[n] = 0;
        if (text && MEM32(g_esp) != 0x0018EA4Eu)
            debug_text_put_from(out, MEM32(g_esp));
        if (s_game_log) {
            while (n && (out[n-1] == '\n' || out[n-1] == '\r')) n--;
            out[n] = 0;
            fprintf(stderr, "[GAME] %s  (from 0x%08X)\n", out, MEM32(g_esp)); fflush(stderr);
        }
    }
    g_esp += 4;   /* cdecl: pop return address only */
}

/* cdecl void DebugColor(int r, int g, int b) @ 0x18F570: the debug text colour. */
extern void sub_0018F570_gen(void);
void sub_0018F570(void)
{
    if (debug_text_on())
        debug_text_color((int)MEM32(g_esp + 4), (int)MEM32(g_esp + 8), (int)MEM32(g_esp + 12));
    sub_0018F570_gen();
}

/* cdecl void DebugLocate(short col, short row) @ 0x18F5C0: the debug text cursor (0x43573C/E). */
extern void sub_0018F5C0_gen(void);
void sub_0018F5C0(void)
{
    if (debug_text_on())
        debug_text_locate((int16_t)MEM16(g_esp + 4), (int16_t)MEM16(g_esp + 8));
    sub_0018F5C0_gen();
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

extern void sub_0013DF00(void);
/* The scene object an animation script moves: CAE::GetScript (0x13DF00, thiscall on the scene's animation
 * engine [scene+0x38]) finds the script by path name; the script's +4 is its root object. 0 if not found. */
static uint32_t video_row_script_obj(uint32_t menu, uint32_t name_va)
{
    uint32_t save = g_esp, ecx = g_ecx, eax = g_eax, scene, engine, script;
    scene = MEM32(menu + 0x38);
    engine = scene ? MEM32(scene + 0x38) : 0;
    if (!engine || !MEM32(engine + 0x7C))
        return 0;
    g_esp -= 4; MEM32(g_esp) = name_va;
    g_esp -= 4; MEM32(g_esp) = 0;   /* return address (unused: the call returns to C) */
    g_ecx = engine;
    sub_0013DF00();
    script = g_eax;
    g_esp = save; g_ecx = ecx; g_eax = eax;
    return script ? MEM32(script + 4) : 0;
}

/* The cut row's label has no always-on object (the list's opening animation, |w_optmenu00, leaves it out):
 * only its focus animation (0x2708A0[4][0], "...|Loc_SaveSet|SaveSet|c_Forcus") gives the label object its
 * sprite, and the unfocus resets it to a blank 16x16 default. While another row is selected, keep that
 * animation playing so VIDEO SETTINGS stays readable. Seen running: it shows white (the focus colour; writing a
 * darker colour does not stick while the animation runs) and its hexagon slot stays empty. */
extern void sub_00137F60(void);
static void video_row_keep_label(uint32_t menu, int left)
{
    static unsigned s_wait;
    uint32_t name = MEM32(0x2708A0 + 4 * 16), obj = video_row_script_obj(menu, name);
    uint32_t save, ecx, eax;
    if (left)
        s_wait = 0;                                     /* the unfocus just stopped it: play again now */
    else if (!obj || MEM32(obj + 0xE0) != 0x41800000u) {   /* sprite applied (width is no longer the 16.0 default) */
        s_wait = 0;
        return;
    }
    if (s_wait && --s_wait)                             /* just asked: give the animation a few frames to apply */
        return;
    save = g_esp; ecx = g_ecx; eax = g_eax;            /* FUN_00137F60(scene, name, -1, 0): play, cdecl */
    g_esp -= 4; MEM32(g_esp) = 0;
    g_esp -= 4; MEM32(g_esp) = 0xFFFFFFFFu;
    g_esp -= 4; MEM32(g_esp) = name;
    g_esp -= 4; MEM32(g_esp) = MEM32(menu + 0x38);
    g_esp -= 4; MEM32(g_esp) = 0;                       /* return address (unused: returns to C) */
    sub_00137F60();
    g_esp = save; g_ecx = ecx; g_eax = eax;
    s_wait = 8;
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
    else if (MEM8(0x3EA597) != 4)
        video_row_keep_label(MEM32(g_esp), before == 4);   /* the original's ret popped its return address: [esp] = menu */
}

/* ---- Developer debug menu (research: kit reports/research/debug-menu-notes.md) ----
 * Game init (0x113B0) always creates a "MENU" task ([0x299B04]) with the developers' menu table 0x227730
 * (MAKINO MENU, HASHI/MINE/KONDOH/MATTY/KOMATSUO TEST ...). Its idle mode 0x1B9F0 switches to the debug
 * menu mode 0x1B900 when the START task ([0x2A8DE0]) has both boot steps at 8; in the retail game that never
 * happens. RECOMP_DEBUG_MENU=1: F9 (game window focused) makes the idle mode switch, exactly as its own code
 * does (+0x20 = 0x1B900, +0x34 = 0, 0x17E2D0(0x298A40, task)). Unset = the original. */
extern void sub_0001B9F0_gen(void);
extern void sub_0017E2D0(void);
extern short __stdcall GetAsyncKeyState(int vk);
extern void *__stdcall GetForegroundWindow(void);
extern unsigned long __stdcall GetWindowThreadProcessId(void *hwnd, unsigned long *pid);
extern unsigned long __stdcall GetCurrentProcessId(void);
static int s_debug_menu = -1;
static volatile int s_dbgmenu_open_req;

static int debug_menu_on(void)
{
    if (s_debug_menu < 0) {
        const char *e = getenv("RECOMP_DEBUG_MENU");
        s_debug_menu = e && *e == '1';
        if (s_debug_menu) {
            fprintf(stderr, "[DEBUG-MENU] on: F9 opens the developers' debug menu\n");
            fflush(stderr);
        }
    }
    return s_debug_menu;
}

/* F9: the Product debug menu (mode 0x1B900). F8: the developers' F026 menu (mode 0x1B640, table 0x227730). */
static int s_dbgmenu_dev;
static int debug_menu_key(void)
{
    static int s_was, s_was8;
    unsigned long pid = 0;
    int down = (GetAsyncKeyState(0x78) & 0x8000) != 0, rose = down && !s_was;     /* VK_F9 */
    int down8 = (GetAsyncKeyState(0x77) & 0x8000) != 0, rose8 = down8 && !s_was8; /* VK_F8 */
    s_was = down;
    s_was8 = down8;
    if (!rose && !rose8)
        return 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId())
        return 0;
    s_dbgmenu_dev = rose8;
    return 1;
}

void sub_0001B9F0(void)
{
    uint32_t task, save;
    if (!debug_menu_on() || !debug_menu_key()) {
        sub_0001B9F0_gen();
        return;
    }
    task = MEM32(g_esp + 4);
    fprintf(stderr, "[DEBUG-MENU] F%d: task 0x%08X -> %s\n", s_dbgmenu_dev ? 8 : 9, task,
            s_dbgmenu_dev ? "developers' F026 menu (0x1B640)" : "Product debug menu (0x1B900)");
    fflush(stderr);
    s_dbgmenu_task = task;
    s_dbgmenu_open_req = !s_dbgmenu_dev;
    if (s_dbgmenu_dev) {
        /* The F026 lists end at the first entry whose function is 0x161680, but the retail build pointed
         * every removed test at that same emptied DebugPrintf, so each list stopped after its first removed
         * entry. Blank the removed ones (the menu skips function 0) and mark the real end. Guest memory
         * only; the files are untouched. */
        int i;
        MEM32(0x227730 + 1 * 8) = 0;                          /* SHUJI TEST (removed); KIRIYAMA stays the end */
        for (i = 5; i <= 8; i++)
            MEM32(0x2418B0 + i * 8) = 0;                      /* SYSTEM LOAD / SAVE / LOAD TEST, SAVEDATA VIEWER */
        MEM32(0x2418B0 + 13 * 8) = 0x161680u;                 /* end after TGS - C */
    }
    MEM32(task + 0x20) = s_dbgmenu_dev ? 0x1B640u : 0x1B900u;
    MEM32(task + 0x34) = 0;
    save = g_esp;
    g_esp -= 4; MEM32(g_esp) = task;
    g_esp -= 4; MEM32(g_esp) = 0x298A40;
    g_esp -= 4; MEM32(g_esp) = 0;   /* return address (unused) */
    sub_0017E2D0();
    g_esp = save + 4;               /* the original's ret */
}

/* Debug-menu mode @ 0x1B900, step +0x34: 0 = set up, 1 = closed (opens after Start+Back are held ~1.8 s),
 * 2 = open the menu (0x1B5F0 builds it from the task's table). F9 asks for step 2 directly, since our PC
 * options overlay already uses Back+Start. */
extern void sub_0001B900_gen(void);

void sub_0001B900(void)
{
    uint32_t t = MEM32(g_esp + 4);
    if (debug_menu_on()) {
        if (debug_menu_key())
            s_dbgmenu_open_req = 1;
        if (s_dbgmenu_open_req && MEM32(t + 0x34) == 1) {
            s_dbgmenu_open_req = 0;
            MEM32(t + 0x34) = 2;
            fprintf(stderr, "[DEBUG-MENU] F9: open the menu (table 0x%08X)\n", MEM32(t + 0x38));
            fflush(stderr);
        }
    }
    sub_0001B900_gen();
}

/* In-game "Debug Mode Select" (EVENT_DEBUG task [0x2A8DDC], draw 0x25AD0). Gameplay start calls
 * 0x258D0(debug switch) from the game task (0x1C130), but in retail the task only exists after MATTY TEST
 * (0x110F30) created it with 0x25FA0(parent). With RECOMP_DEBUG_MENU=1 and the switch on (DEGUG ON in the F9
 * menu), create it under the calling game task (still in esi at the call) so pause shows the debug page. */
extern void sub_000258D0_gen(void);
extern void sub_00025FA0(void);
void sub_000258D0(void)
{
    if (debug_menu_on() && MEM32(g_esp + 4) == 1 && MEM32(0x2A8DDC) == 0 && g_esi) {
        uint32_t save = g_esp, eax = g_eax, ecx = g_ecx, edx = g_edx;
        g_esp -= 4; MEM32(g_esp) = g_esi;
        g_esp -= 4; MEM32(g_esp) = 0;   /* return address (unused) */
        sub_00025FA0();
        g_esp = save; g_eax = eax; g_ecx = ecx; g_edx = edx;
        fprintf(stderr, "[DEBUG-MENU] in-game debug task created: 0x%08X (parent 0x%08X)\n", MEM32(0x2A8DDC), g_esi);
        fflush(stderr);
    }
    sub_000258D0_gen();
}

/* In-game Debug Mode Select = the EVENT_DEBUG task's draw (0x25AD0); it shows while the game is paused. Its own
 * Start toggles that pause, but the game task's pause handler sees the same Start and ends it in the same frame
 * (seen: USER PAUSE / USER PAUSE END together), so it never shows. F7 is a Start press that only the game task
 * (0x1C130) sees: it reads Start from the pad block at 0x29BEB0 (+4 = pressed), the page from 0x2988F0. The game
 * task then pauses and unpauses through its own path (calling 0x21B90/0x21BB0 directly worked, but skipped
 * 0x11770 and left the pause menu's help line on screen in play, following the keys). */
extern void sub_0001C130_gen(void);
void sub_0001C130(void)
{
    static int s_was7, s_f7_close;
    int d7 = (GetAsyncKeyState(0x76) & 0x8000) != 0;   /* VK_F7 */
    if (debug_menu_on() && d7 && !s_was7 && MEM32(0x2A8DDC)) {
        unsigned long pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) {
            if (MEM32(0x29F6E0) == 0) {
                MEM32(MEM32(0x2A8DDC) + 0xA8) = 0;   /* open on the switch list (White; Black = value list) */
                MEM32(0x29BEB4) |= 0x10;             /* Start: pause */
            } else
                s_f7_close = 3;   /* paused: the pause menu task (0x1275F0) owns the exit; press B for it */
        }
    }
    if (s_f7_close > 0) {
        if (MEM32(0x29F6E0) == 1) {
            MEM32(0x2988F0) |= 0x200;   /* the pause menu reads 0x2988F0: held, pressed, repeat */
            MEM32(0x2988F4) |= 0x200;
            MEM32(0x2988F8) |= 0x200;
            s_f7_close--;
        } else
            s_f7_close = 0;
    }
    s_was7 = d7;
    sub_0001C130_gen();
}
/* Debug Mode Select switches whose code the retail game dropped: "Player NoDamage" (bit 16) and "Player FastMove"
 * (bit 21) of [EVENT_DEBUG task + 0x48]. Nothing in retail reads those bits (only the getters 0x25930..0x25A90
 * exist, for other bits), so they are rebuilt here around the hero's update, 0x3E710 (the TSS_EHP_MAN task;
 * hero = [0x2A92D0]).
 *   NoDamage: the hero's status block is 0x4FF20(hero+0x142); life is a float at +0x5AB0, max at +0x5AB4 (the
 *     update hands both to the HUD gauge, 0x12A3C0/0x12A3E0). Life is held at max before and after each update,
 *     and the two "change life" helpers (0x48D50, 0x48DA0) ignore losses for the hero while the switch is on.
 *   FastMove: the hero's position is status +0x00 (x) / +0x08 (z) (the task's matrix at +0x60 is a copy). The
 *     horizontal step taken between two updates is stretched (RECOMP_FASTMOVE_SCALE, default 2); big jumps
 *     (warps, loads) are left alone. */
#include "mem_search_host.h"
#include "tuning_host.h"
extern void sub_0003E710_gen(void);
extern void sub_00048D50_gen(void);
extern void sub_00048DA0_gen(void);
extern void sub_0004FF20(void);
static int dbg_switch(int bit)
{
    uint32_t t = MEM32(0x2A8DDC);
    return debug_menu_on() && t && ((MEM32(t + 0x48) >> bit) & 1);
}
static uint32_t hero_status(uint32_t hero)
{
    uint32_t save = g_esp, eax = g_eax, ecx = g_ecx, edx = g_edx, st;
    g_esp -= 4; MEM32(g_esp) = (uint32_t)(int32_t)(int16_t)MEM16(hero + 0x142);
    g_esp -= 4; MEM32(g_esp) = 0;   /* return address (unused) */
    sub_0004FF20();
    st = g_eax;
    g_esp = save; g_eax = eax; g_ecx = ecx; g_edx = edx;
    return st;
}
static uint32_t s_hero_life_block;   /* status + 0x5AAC of the hero, refreshed by every hero update */
static void hero_fill_life(uint32_t st)
{
    if (st && MEMF(st + 0x5AB4) > 0.0f && MEMF(st + 0x5AB0) < MEMF(st + 0x5AB4))
        MEMF(st + 0x5AB0) = MEMF(st + 0x5AB4);
}
static float fastmove_scale(void)
{
    static float s = -1.0f;
    if (s < 0.0f) {
        const char *e = getenv("RECOMP_FASTMOVE_SCALE");
        s = e && *e ? (float)atof(e) : 2.0f;
        if (s < 1.0f || s > 8.0f)
            s = 2.0f;
    }
    return s;
}
void sub_0003E710(void)
{
    static uint32_t s_fm_st;
    static float s_fm_x, s_fm_z;
    uint32_t task = MEM32(g_esp + 4), hero = MEM32(0x2A92D0), st = 0;
    int nodmg = 0;
    float move_k = 0.0f;
    if (task && task == hero) {
        nodmg = dbg_switch(16);
        /* Step stretch: the FastMove switch times the tuning range's "Move speed x" (src/tuning_host.c). */
        move_k = (dbg_switch(21) ? fastmove_scale() : 1.0f) * tuning_host_move_speed() - 1.0f;
        if (nodmg || move_k > 1e-3f || move_k < -1e-3f || tuning_host_active())
            st = hero_status(hero);
        tuning_host_hero(st);   /* the window reads this; finalized values are applied once per new hero */
        if (nodmg) {
            s_hero_life_block = st ? st + 0x5AAC : 0;
            mem_search_name("herolife", st ? st + 0x5AB0 : 0);
            hero_fill_life(st);
        }
        if (st)
            mem_search_name("herostatus", st);
        if (st && (move_k > 1e-3f || move_k < -1e-3f)) {
            /* The hero's position is status +0x00 (x) / +0x08 (z). Stretch the step taken since the last
             * update; remember the stretched position so the next step is measured from it. */
            float x = MEMF(st + 0x00), z = MEMF(st + 0x08);
            if (s_fm_st == st) {
                float dx = x - s_fm_x, dz = z - s_fm_z, k = move_k, d2 = dx * dx + dz * dz;
                if (d2 > 1e-6f && d2 < 40.0f * 40.0f) {   /* a normal step, not a warp */
                    x += dx * k;
                    z += dz * k;
                    MEMF(st + 0x00) = x;
                    MEMF(st + 0x08) = z;
                }
            }
            s_fm_st = st;
            s_fm_x = x;
            s_fm_z = z;
        } else
            s_fm_st = 0;
    }
    sub_0003E710_gen();
    if (nodmg)
        hero_fill_life(st);
    /* "Player Position Disp" (bit 13) only prints from the developers' free-fly camera (0x1E5C0); in normal play
     * show the hero's position and life on the bottom line instead. */
    if (task && task == hero && dbg_switch(13)) {
        char line[96];
        if (!st)
            st = hero_status(hero);
        if (st) {
            snprintf(line, sizeof line, "POS %.1f %.1f %.1f  LIFE %.0f/%.0f", MEMF(st + 0x00), MEMF(st + 0x04),
                     MEMF(st + 0x08), MEMF(st + 0x5AB0), MEMF(st + 0x5AB4));
            debug_text_locate(2, 28);
            debug_text_put(line);
        }
    }
}
/* 0x48D50(life block, amount) and 0x48DA0(life block, source, amount): add to life, clamped to [0, max]. */
void sub_00048D50(void)
{
    uint32_t blk = MEM32(g_esp + 4);
    if (blk && blk == s_hero_life_block && MEMF(g_esp + 8) < 0.0f && dbg_switch(16))
    {
        g_esp += 4;   /* ret, as the generated function would */
        return;   /* NoDamage: the hero's life does not go down */
    }
    sub_00048D50_gen();
}
void sub_00048DA0(void)
{
    uint32_t blk = MEM32(g_esp + 4);
    if (blk && blk == s_hero_life_block && MEMF(g_esp + 12) < 0.0f && dbg_switch(16)) {
        g_esp += 4;
        return;
    }
    sub_00048DA0_gen();
}

/* ---- Skills list: combo rows (S10.11). RECOMP_SKILL_COMBOS=1 turns it on; =test also adds a row with every
 * button picture the game has (11..15) so a screenshot shows which is which.
 * The panel (pause menu > Inventory & Skills > Skills) keeps: +0xC4 scroll, +0xC8 last scroll, +0xCC rows,
 * +0xCD visible rows (max 5), +0xCE scroll range, +0x44 its layout, +0xB0[5] one text object per visible row.
 * 0x123C90 sets the rows from story progress (up to 7, text ids 0x107..0x10D); 0x11ECF0 gives row r the text
 * id 0x107 + scroll + r; 0x11E950 sets the row's gauge; 0x11E7F0 draws the row's picture (7 entries).
 * Combo rows come after the learned ones and use text ids SKC_ID0.., which 0x1372F0 (text by id) and 0x137630
 * (its style) answer from our own copy in guest memory. ---- */
#include "skill_combos.h"
extern void sub_00123C90_gen(void);
extern void sub_0011ECF0_gen(void);
extern void sub_0011E950_gen(void);
extern void sub_0011E7F0_gen(void);
extern void sub_001372F0_gen(void);
extern void sub_00137630_gen(void);
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
#define SKC_ID0     0x7000u
#define SKC_WORDS   96u
static int s_skc_mode = -1;   /* 0 off, 1 on, 2 on + test row */
static int s_skc_learned = 7;  /* the game's own rows in the open panel */
static int s_skc_rows;         /* our rows */
static uint32_t s_skc_text;    /* guest copy: s_skc_rows strings of SKC_WORDS words */
static int skc_mode(void)
{
    if (s_skc_mode < 0) {
        const char *e = getenv("RECOMP_SKILL_COMBOS");
        s_skc_mode = !e || !*e || *e == '0' ? 0 : (strcmp(e, "test") == 0 ? 2 : 1);
    }
    return s_skc_mode;
}
static void skc_build(void)
{
    /* Seen on screen with the test row (layout 0): guide13 = right trigger = Attack 1, guide14 = left trigger =
     * Attack 2. The picture follows the player's button layout, as the game's own lines do. */
    struct skill_combo_pics pics = { 13, 14 };
    uint16_t buf[SKC_WORDS];
    int i, n = skill_combo_count() + (skc_mode() == 2);
    if (s_skc_text)
        return;
    s_skc_text = xbox_HeapAlloc((uint32_t)n * SKC_WORDS * 2u, 16);
    if (!s_skc_text)
        return;
    for (i = 0; i < n; i++) {
        const char *spec = i < skill_combo_count() ? skill_combo_spec(i) : skill_combo_test_spec();
        size_t w, k = skill_combo_encode(spec, &pics, buf, SKC_WORDS);
        if (!k) { buf[0] = SC_END; k = 1; }
        for (w = 0; w < k; w++)
            MEM16(s_skc_text + (uint32_t)(i * SKC_WORDS + w) * 2u) = buf[w];
    }
    s_skc_rows = n;
    fprintf(stderr, "[SKILLS] %d combo rows ready at 0x%08X\n", n, s_skc_text);
}
static int skc_id(uint32_t id)
{
    return s_skc_rows && id >= SKC_ID0 && id < SKC_ID0 + (uint32_t)s_skc_rows ? (int)(id - SKC_ID0) : -1;
}
static void skc_call(void (*fn)(void), uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, int nargs)
{
    uint32_t save = g_esp;
    if (nargs > 3) { g_esp -= 4; MEM32(g_esp) = a4; }
    if (nargs > 2) { g_esp -= 4; MEM32(g_esp) = a3; }
    if (nargs > 1) { g_esp -= 4; MEM32(g_esp) = a2; }
    if (nargs > 0) { g_esp -= 4; MEM32(g_esp) = a1; }
    g_esp -= 4; MEM32(g_esp) = 0;   /* return address (unused: returns to C) */
    fn();
    g_esp = save;
}
/* Skills tab opened: the game counts its learned rows; add ours after them. */
void sub_00123C90(void)
{
    uint32_t panel = MEM32(g_esp + 4), ret, ebx = g_ebx, esi = g_esi, edi = g_edi;
    int learned, total, cd_old, cd, i;
    sub_00123C90_gen();
    if (!skc_mode())
        return;
    skc_build();
    if (!s_skc_rows)
        return;
    ret = g_eax;
    learned = (int8_t)MEM8(panel + 0xCC);
    s_skc_learned = learned < 0 ? 0 : learned;
    total = s_skc_learned + s_skc_rows;
    if (total > 120) total = 120;
    cd_old = (int8_t)MEM8(panel + 0xCD);
    cd = total < 5 ? total : 5;
    MEM8(panel + 0xCC) = (uint8_t)total;
    MEM8(panel + 0xCD) = (uint8_t)cd;
    MEM8(panel + 0xCE) = (uint8_t)(total - cd);
    for (i = cd_old < 0 ? 0 : cd_old; i < cd; i++) {   /* show the row slots the game left hidden, as it does */
        uint32_t slot = 0x1Au + (uint32_t)i;
        skc_call(sub_0001FE80, 0, 0, 0, 0, 0);
        if (g_eax != 1)
            slot = (uint32_t)(int32_t)SMEM16(0x218B30 + slot * 2u);
        skc_call(sub_00137FF0, MEM32(panel + 0x44), slot, 1, 0, 4);
        g_eax = (uint32_t)i; g_ecx = panel;
        skc_call(sub_0011E950, 0, 0, 0, 0, 0);
    }
    MEM32(panel + 0xD0) = 1;   /* refresh the row texts */
    g_eax = ret; g_ebx = ebx; g_esi = esi; g_edi = edi;
    fprintf(stderr, "[SKILLS] list: %d learned + %d combo rows\n", s_skc_learned, s_skc_rows);
}
/* Row text (esi = panel, edi = row): a combo row asks for our id instead of 0x107 + its index. */
void sub_0011ECF0(void)
{
    uint32_t panel = g_esi, scroll;
    int idx;
    if (!skc_mode() || !s_skc_rows) { sub_0011ECF0_gen(); return; }
    scroll = MEM32(panel + 0xC4);
    idx = (int)scroll + (int)g_edi;
    if (idx < s_skc_learned) { sub_0011ECF0_gen(); return; }
    MEM32(panel + 0xC4) = SKC_ID0 + (uint32_t)(idx - s_skc_learned) - 0x107u - g_edi;
    sub_0011ECF0_gen();
    MEM32(panel + 0xC4) = scroll;
}
/* Row gauge (eax = row, ecx = panel): its table has 7 entries; a combo row uses the first skill's. */
void sub_0011E950(void)
{
    uint32_t panel = g_ecx, scroll;
    int row = (int)g_eax;
    if (!skc_mode() || !s_skc_rows) { sub_0011E950_gen(); return; }
    scroll = MEM32(panel + 0xC4);
    if ((int)scroll + row < s_skc_learned) { sub_0011E950_gen(); return; }
    MEM32(panel + 0xC4) = (uint32_t)(-row);
    sub_0011E950_gen();
    MEM32(panel + 0xC4) = scroll;
}
/* Row picture (arg = row, esi = panel): 7 pictures; a combo row has none. Keep the original's last step. */
void sub_0011E7F0(void)
{
    uint32_t panel = g_esi;
    int row = (int)MEM32(g_esp + 4);
    if (skc_mode() && s_skc_rows && (int)MEM32(panel + 0xC8) + row >= s_skc_learned) {
        MEM32(panel + 0xC8) = MEM32(panel + 0xC4);
        g_esp += 4;
        return;
    }
    sub_0011E7F0_gen();
}
/* Text by id (bank, id) -> pointer to its first code. */
void sub_001372F0(void)
{
    int k = skc_id(MEM32(g_esp + 8));
    if (k < 0) { sub_001372F0_gen(); return; }
    g_eax = s_skc_text + (uint32_t)k * SKC_WORDS * 2u;
    g_esp += 4;
}
/* Text style by id (bank, id): ours use the style of the first skill line. */
void sub_00137630(void)
{
    uint32_t id = MEM32(g_esp + 8);
    if (skc_id(id) < 0) { sub_00137630_gen(); return; }
    MEM32(g_esp + 8) = 0x107;
    sub_00137630_gen();   /* pops its return address; the argument slot is the caller's to clean */
    MEM32(g_esp + 4) = id;
}
