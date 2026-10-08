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
#include <stdint.h>
/* The host controller layer (xboxrecomp src/input) behind the XInput
 * overrides below. Included before recomp_funcs.h: it pulls in windows.h, and
 * the register-alias macros that header defines (eax, esp, ...) must not be in
 * force while the Windows headers are parsed. */
#include "xinput_xbox.h"
/* Needed for the eax/ecx/esi/... -> g_eax/g_ecx/g_esi/... register-alias
 * macros in recomp_types.h, which are gated behind this define (same as
 * every generated recomp_NNNN.c chunk file) -- required for the
 * sub_001DA7DB override below to use the same register names as the
 * translation it replaces. */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"

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
/* g_eax, g_esp, g_xbox_mem_offset, recomp_func_t all come from the
 * recomp_funcs.h/recomp_types.h include above -- redeclaring g_eax/g_esp
 * without RECOMP_TLS here used to conflict with that (storage-class
 * mismatch), so those two are gone; keep using them exactly as before,
 * they're still in scope. */

/* ── Manual function overrides ─────────────────────────────── */

/*
 * sub_001A81DA override -- REVERTED (2026-09-02).
 *
 * Was: a hand-written body that added 24 to g_esp and returned normally,
 * to suppress the reboot this function performs so boot could be observed
 * further. Verified wrong: xbox_HalReturnToFirmware (src/kernel/kernel_hal.c,
 * xboxrecomp-fork) already calls ExitProcess(0) and never returns -- that
 * is correct emulation of real hardware, not a bug. The override made a
 * genuinely no-return function return into a caller chain that was never
 * written expecting control back, and that produced a WORSE failure than
 * the clean exit it replaced: ordinals 277/294 (RtlEnterCriticalSection /
 * RtlLeaveCriticalSection) ping-ponged on stale return addresses
 * (0x1A9B43 / 0x1AA0F4) until Wine reported a stack overflow.
 *
 * Root cause of the reboot itself (still real, still open, tracked on
 * HeroLab): sub_001A80B7 (asm at 0x001A80B7) scans a one-entry function-
 * pointer table at guest VA 0x00227724 -- if [0x227724] is non-zero it
 * calls that pointer directly (0x001A80EA: `call dword ptr [ecx]`), else
 * it falls through to sub_001ADCD7 with path=NULL, which lands in this
 * function, which unconditionally calls HalReturnToFirmware(2). Full-binary
 * grep of tools/disasm/output/asm/text.asm (both the address literal and
 * its raw LE bytes `24 77 22 00`) shows nothing in Breakdown's own code
 * ever writes that slot -- so on any run through our recomp it is always
 * empty and this path is always taken. Whatever registers a device there
 * on real hardware (kernel-side driver init, or an XDK CRT-startup routine
 * not yet recovered by disasm) is not implemented in xboxrecomp's kernel
 * bridge. That's a real emulation gap, but implementing a fake device
 * registration to force a specific outcome would be worse than this
 * revert -- it would need to be correct, not a guess, and no evidence
 * currently points at what device or from where. Left as a stub-free
 * generated function; do not re-add an override without new evidence.
 */

/*
 * sub_001DA7DB override -- direct replacement (2026-09-04), not a
 * recomp_lookup_manual() redirect: this function is defined here under its
 * real name, and --exclude-manual (regenerate.sh) skips generating the
 * auto-translated version, so every existing call site and vtable/dispatch
 * entry that already references sub_001DA7DB links straight to this one.
 *
 * WHY: the original guest code (0x001DA7DB-0x001DA84D) is an APU per-voice
 * "kick and wait for hardware to acknowledge" handshake:
 *
 *   eax = MEM32(esi);  eax = MEM32(eax*4 + 0x1DB440);   // per-voice-type
 *                                                        // register address,
 *                                                        // via a lookup table
 *   MEM8(eax - 0x13FFEF5) = 2;                           // kick: set busy bit
 *   ecx = MEM8(eax - 0x13FFEF5) & 2;                     // read it back
 *   while (ecx) ;                 // <-- THE BUG: re-tests the REGISTER
 *                                  //     captured by the read above, never
 *                                  //     re-reading memory. This can only
 *                                  //     ever execute 0 or 1 extra
 *                                  //     iterations on real hardware,
 *                                  //     because the MMIO write is
 *                                  //     synchronously acknowledged by the
 *                                  //     APU before software's very next
 *                                  //     instruction reads it back.
 *
 * This runtime has no such latency: the write and the read-back are two
 * back-to-back MEM8 macro calls with nothing in between, so no background
 * "hardware" thread (see MCPX_ACK / MCPX_IDLE, xboxrecomp-fork's
 * xbox_memory_layout.c) can ever win the race to clear the bit first --
 * every single call captures the bit as freshly-set-2, and `while(ecx)`
 * spins on that stale captured value forever. Confirmed live: gdb RIP
 * sampling on the actually-stuck thread pointed straight at this loop
 * (0x140996398, addr2line -> sub_001DA7DB, recomp_0011.c:46939), and every
 * one of the candidate hardware-register addresses read back 0x00 moments
 * later -- the MCPX_ACK background thread WAS clearing them, just never
 * fast enough to matter, because there was never a real window for it to
 * act in. HeroLab, Xbox Recompiler project, task tracked 2026-09-04.
 *
 * This override is the SAME translation the lifter already produces
 * (copied verbatim from a clean regen of recomp_0011.c to guarantee ABI
 * fidelity -- same PUSH32/POP32 simulated-stack sequence, same callee
 * calling conventions), with exactly one change: the dead self-loop
 * (`if (TEST_NZ(_fa, _fb)) goto loc_001DA811;`) is removed. Proceeding
 * unconditionally after the read-back is the correct emulation of what
 * real hardware actually guarantees at this point, not a guess -- the
 * loop could never do anything else useful even if left in, since it does
 * not re-read memory on any iteration.
 */
void sub_001DA7DB(void)
{
    uint32_t ebp;
    int _flags = 0; /* fallback flag var */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_flags; (void)_fa; (void)_fb; (void)_fas; (void)_fbs;

    PUSH32(esp, ebp);
    ebp = esp;
    g_ebp = ebp; /* publish frame for frameless callees */
    PUSH32(esp, ecx);
    PUSH32(esp, ecx);
    MEM32(ebp + -4) = MEM32(ebp + -4) & 0;
    PUSH32(esp, esi);
    esi = ecx;
    ecx = ebp + -8;
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001DA7EFu); sub_001D12F0(); /* call 0x001D12F0 */

    eax = MEM32(esi);
    eax = MEM32(eax * 4 + 0x1DB440);
    MEM8(eax + -20971253) = 2;
    eax = MEM32(esi);
    eax = MEM32(eax * 4 + 0x1DB440);
    SET_LO8(ecx, MEM8(eax + -20971253));
    SET_LO8(ecx, LO8(ecx) & 2);

    /* Original self-loop removed here -- see comment above. */

    ecx = MEM32(esi + 0x1C);
    MEM32(eax + -20971264) = ecx;
    _fa = (uint32_t)(MEM32(esi)) & 0xFFFFFFFFu; _fb = (uint32_t)(1) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb);
    if (CMP_NE(_fa, _fb)) goto loc_001DA82B;

    eax = MEM32(esi + 0x28);
    MEM32(0xFEC0017Cu) = eax;

loc_001DA82B: ;
    eax = 0; /* xor self */
    SET_LO8(eax, MEM8(esi + 0x24));
    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = esi;
    PUSH32(esp, eax);
    eax = 0; /* xor self */
    SET_LO8(eax, MEM8(esi + 0x25));
    PUSH32(esp, eax);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001DA842u); sub_001DA5E5(); /* call 0x001DA5E5 */

    ecx = ebp + -8;
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001DA84Au); sub_001D1312(); /* call 0x001D1312 */

    POP32(esp, esi);
    esp = ebp;
    POP32(esp, ebp); /* leave */
    esp += 4; return; /* ret */
}

/*
 * sub_001D3FD9 override -- direct replacement (2026-09-04), same pattern
 * and precedent as sub_001DA7DB above: defined here under its real name,
 * and --exclude-manual (regenerate.sh) skips generating the auto-translated
 * version, so every call site and vtable/dispatch entry that references
 * sub_001D3FD9 links straight to this one (also re-registered in
 * recomp_lookup_manual() below for the ICALL/indirect-dispatch path --
 * see the comment there).
 *
 * WHY: this is the hot-spin blocker found while boot investigation got
 * past NuSoundStream init (~28k kernel calls in, no crashes, most worker
 * threads genuinely asleep). Live gdb RIP sampling on the one thread
 * pinned at ~100% CPU pointed at the loop at guest 0x001D4140-0x001D4143
 * (asm: tools/disasm/output/asm/DSOUND.asm, DSOUND section):
 *
 *   0x001D4136  add  ebx, 0x810
 *   0x001D413E  mov  dword ptr [ebx], eax     ; eax == 3 here (push 3; pop eax
 *                                              ;   earlier in this function)
 *   0x001D4140  cmp  dword ptr [ebx], 0
 *   0x001D4143  jne  0x1d4140                 ; spin until hardware zeroes it
 *
 * This is an APU DMA "kick and wait for hardware to consume the command"
 * handshake, structurally the same idea as sub_001DA7DB's per-voice kick,
 * but NOT the same fix category as MCPX_ACK/MCPX_IDLE/NV2A_ACK (xboxrecomp-
 * fork's xbox_memory_layout.c): those tables cover fixed MMIO addresses.
 * Here the polled address is dynamic guest HEAP memory -- a live register
 * dump of the stuck thread showed the polled address around guest VA
 * 0x010E8810 (heap range 0x00F80000-0x04000000), with rax holding
 * g_xbox_mem_offset (0x10000) confirming it's a heap access, not MMIO. The
 * static offset tables cannot address a per-allocation dynamic location
 * like this, so option A (a manual override removing the dead loop) is
 * used instead of trying to extend those tables -- option B, an actual
 * APU/DMA command-queue model reacting to the write and clearing the word
 * asynchronously, is the correct long-term fix but out of scope here.
 *
 * Unlike sub_001DA7DB's loop, this one DOES re-read memory every
 * iteration (`cmp dword ptr [ebx], 0` reads fresh each time, it is not
 * testing a stale captured register) -- so this is not the same bug
 * pattern of a translation artifact. It is real: the guest wrote a
 * command word (3) into what would be an APU DMA command ring/mailbox
 * slot and is legitimately waiting for the audio hardware to consume and
 * clear it. This runtime has no APU model, so nothing will ever clear
 * that word, and the wait never terminates. Real hardware clears it
 * (analogous to the MCPX kick/ack cases already documented above) --
 * proceeding unconditionally past this point is standing in for that
 * hardware behavior, not skipping something guest software depends on
 * seeing loop.
 *
 * This override is the SAME translation the lifter already produces
 * (copied verbatim from a clean regen of recomp_0011.c, sub_001D3FD9, to
 * guarantee ABI fidelity -- same PUSH32/POP32 simulated-stack sequence,
 * same callee calling conventions), with exactly one change: the dead
 * self-loop at loc_001D4140 (`if (CMP_NE(_fa, _fb)) goto loc_001D4140;`)
 * is removed, falling through to loc_001D4145 unconditionally.
 */
void sub_001D3FD9(void)
{
    uint32_t ebp;
    int _flags = 0; /* fallback flag var */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;

loc_001D3FD9: ;
    PUSH32(esp, ebp);
    ebp = esp;
    g_ebp = ebp; /* publish frame for frameless callees */
    esp = esp - 0x1C;
    PUSH32(esp, ebx);
    PUSH32(esp, esi);
    PUSH32(esp, edi);
    edi = ecx;
    eax = MEM32(edi + 8);
    eax = MEM32(eax + 0x10);
    ebx = MEM32(eax);
    eax = MEM32(ebx + 0x80C);
    eax = eax + MEM32(ebx + 0x804);
    ecx = MEM32(edi + 0x20);
    esi = ebx + eax * 4 + 0x818;
    eax = MEM32(esi);
    eax--;
    eax = eax << 5;
    eax = eax + 0x28;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(ecx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test ecx, ecx (32-bit) */
    MEM32(ebp + -20) = edi;
    MEM32(ebp + -16) = eax;
    if (TEST_Z(_fa, _fb)) goto loc_001D4022; /* je: equal / zero */

loc_001D4015: ;
    PUSH32(esp, ecx);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D401Bu); sub_001D3DE2(); /* call 0x001D3DE2 */

loc_001D401B: ;
    MEM32(edi + 0x20) = MEM32(edi + 0x20) & 0;
    eax = MEM32(ebp + -16);

loc_001D4022: ;
    PUSH32(esp, 1);
    PUSH32(esp, eax);
    PUSH32(esp, 0x626F5344);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D402Fu); sub_001D3D7A(); /* call 0x001D3D7A */

loc_001D402F: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    MEM32(edi + 0x20) = eax;
    if (TEST_NZ(_fa, _fb)) goto loc_001D4040; /* jne: not equal / not zero */

loc_001D4036: ;
    eax = 0x8007000Eu;
    goto loc_001D4147;

loc_001D4040: ;
    PUSH32(esp, MEM32(esi + 4));
    ecx = MEM32(edi + 8);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D404Bu); sub_001D9672(); /* call 0x001D9672 */

loc_001D404B: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (TEST_S(_fas, _fbs)) goto loc_001D4147; /* jl: less (signed <) */

loc_001D4053: ;
    eax = ebx + 0x818;
    MEM32(ebp + -4) = eax;
    eax = MEM32(ebp + -16);
    eax = eax + esi;
    MEM32(ebp + -8) = eax;
    eax = ebp + -28;
    PUSH32(esp, eax);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D406Du); sub_001D97C3(); /* call 0x001D97C3 */

loc_001D406D: ;
    eax = MEM32(esi);
    PUSH32(esp, 0);
    PUSH32(esp, MEM32(ebp + -8));
    eax = eax << 3;
    PUSH32(esp, eax);
    PUSH32(esp, MEM32(ebp + -8));
    eax = ebp + -28;
    PUSH32(esp, eax);
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D4084u); sub_001D97F0(); /* call 0x001D97F0 */

loc_001D4084: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (CMP_LE(_fas & _fbs, 0)) goto loc_001D4094; /* jle: less or equal (signed <=) */

loc_001D4088: ;
    eax = eax & 0xFFFF;
    eax = eax | 0x80070000u;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */

loc_001D4094: ;
    if (TEST_S(_fas, _fbs)) goto loc_001D4147; /* jl: less (signed <) */

loc_001D409A: ;
    MEM32(ebp + -12) = MEM32(ebp + -12) & 0;
    _fa = (uint32_t)(MEM32(esi)) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(esi), 0 (32-bit) */
    if (CMP_BE(_fa, _fb)) goto loc_001D4122; /* jbe: below or equal (unsigned <=) */

loc_001D40A3: ;
    eax = MEM32(ebp + -8);
    MEM32(ebp + -8) = eax;
    edi = esi + 8;

loc_001D40AC: ;
    PUSH32(esp, 0);
    PUSH32(esp, MEM32(ebp + -4));
    PUSH32(esp, MEM32(edi + 4));
    PUSH32(esp, MEM32(ebp + -4));
    PUSH32(esp, MEM32(ebp + -8));
    g_ebp = ebp; /* frame stays current across calls */
    PUSH32(esp, 0x001D40BFu); sub_001D97F0(); /* call 0x001D97F0 */

loc_001D40BF: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (CMP_LE(_fas & _fbs, 0)) goto loc_001D40CF; /* jle: less or equal (signed <=) */

loc_001D40C3: ;
    eax = eax & 0xFFFF;
    eax = eax | 0x80070000u;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */

loc_001D40CF: ;
    if (TEST_S(_fas, _fbs)) goto loc_001D4147; /* jl: less (signed <) */

loc_001D40D1: ;
    eax = MEM32(edi + 4);
    MEM32(ebp + -4) = MEM32(ebp + -4) + eax;
    eax = MEM32(ebp + -20);
    ecx = MEM32(eax + 0xC);
    ecx = MEM32(ecx + 4);
    MEM32(edi + 0x10) = MEM32(edi + 0x10) + 0xFE836000u;
    MEM32(ebp + -8) = MEM32(ebp + -8) + 8;
    ecx = ecx - 0x17C6818;
    MEM32(edi) = MEM32(edi) + ecx;
    ecx = 0xFFA0BE7Au;
    ecx = ecx - MEM32(ebx + 0x804);
    edi = edi + 0x20;
    ecx = ecx << 2;
    MEM32(edi + -24) = MEM32(edi + -24) + ecx;
    eax = MEM32(eax + 8);
    eax = MEM32(eax + 0x14);
    eax = eax - 0xC000;
    MEM32(edi + -8) = MEM32(edi + -8) + eax;
    MEM32(ebp + -12) = MEM32(ebp + -12) + 1;
    eax = MEM32(ebp + -12);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(MEM32(esi)) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, MEM32(esi) (32-bit) */
    if (CMP_B(_fa, _fb)) goto loc_001D40AC; /* jb: below (unsigned <) */

loc_001D411F: ;
    edi = MEM32(ebp + -20);

loc_001D4122: ;
    ecx = MEM32(ebp + -16);
    edi = MEM32(edi + 0x20);
    eax = ecx;
    ecx = ecx >> 2;
    memcpy((void*)XBOX_PTR(edi), (void*)XBOX_PTR(esi), ecx * 4);
    esi += ecx * 4; edi += ecx * 4; ecx = 0; /* rep movsd */
    ecx = eax;
    PUSH32(esp, 3);
    POP32(esp, eax);
    ecx = ecx & eax;
    ebx = ebx + 0x810;
    memcpy((void*)XBOX_PTR(edi), (void*)XBOX_PTR(esi), ecx);
    esi += ecx; edi += ecx; ecx = 0; /* rep movsb */
    MEM32(ebx) = eax;

    /* Original self-loop removed here -- see comment above. */

loc_001D4145: ;
    eax = 0; /* xor self */

loc_001D4147: ;
    POP32(esp, edi);
    POP32(esp, esi);
    POP32(esp, ebx);
    esp = ebp;
    POP32(esp, ebp); /* leave */
    esp += 8; return; /* ret 4 */

}

/* ── Controller input: the XDK XInput API, on the host controller ───────
 *
 * XInput on the Xbox is not a kernel export. It is XDK library code linked
 * into the XPP section, and it talks to the USB host controller, which this
 * runtime does not emulate. The device-type tables it maintains never
 * change, so the title's XGetDeviceChanges never reports a pad and the title
 * screen waits for a Start press forever.
 *
 * These replace the six entry points Breakdown's pad layer calls
 * (sub_0018C5A0, once a frame) with the host controller layer in xboxrecomp's
 * src/input: a real controller through XInput, or on port 0 the keyboard
 * stand-in or an XBOXRECOMP_PAD_SCRIPT when none is attached. The addresses
 * and the stdcall argument sizes (from each original's `ret`) were read from
 * the retail XBE. XInitDevices is left alone: the title never sees what it sets
 * up, since every call that would read it is replaced here. The memory-unit
 * path (sub_0019D180) goes through XGetDeviceChanges too and simply never sees
 * a unit, so XMountMU/XUnmountMU are never reached.
 *
 * Layouts (XDK), confirmed against the offsets the pad layer itself reads:
 *   XINPUT_STATE        DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad (18 bytes:
 *                       WORD wButtons, BYTE bAnalogButtons[8], SHORT sThumbLX,
 *                       sThumbLY, sThumbRX, sThumbRY)
 *   XINPUT_CAPABILITIES BYTE SubType; WORD Reserved; XINPUT_GAMEPAD In at +4;
 *                       XINPUT_RUMBLE Out at +22
 *   XINPUT_FEEDBACK     DWORD dwStatus; HANDLE hEvent; BYTE Reserved[58];
 *                       XINPUT_RUMBLE at +66 (the pad layer zeroes +0xD2/+0xD4
 *                       of a record whose feedback starts at +0x90)
 */

#define BD_XDEVICE_TYPE_GAMEPAD  0x00203DD4u    /* XPP device-type table */
#define BD_PAD_HANDLE_BASE       0x58490000u    /* handle = base + port; only
                                                 * these overrides look inside */
#define BD_ERROR_INVALID_HANDLE        6u
#define BD_ERROR_DEVICE_NOT_CONNECTED  0x48Fu
#define BD_ERROR_IO_PENDING            0x3E5u

static int bd_pad_port(uint32_t handle)
{
    uint32_t port = handle - BD_PAD_HANDLE_BASE;
    return port < XBOX_MAX_CONTROLLERS ? (int)port : -1;
}

/* BOOL XGetDeviceChanges(PXPP_DEVICE_TYPE, PDWORD pdwInsertions,
 *                        PDWORD pdwRemovals) -- ret 0xC */
void sub_00204CDD(void)
{
    uint32_t type = MEM32(esp + 4), p_ins = MEM32(esp + 8), p_rem = MEM32(esp + 12);
    uint32_t ins = 0, rem = 0;

    if (type == BD_XDEVICE_TYPE_GAMEPAD) {
        /* Ports already reported present. A pad attached at boot is an
         * insertion on the first call, as on the console. */
        static uint32_t reported;
        uint32_t now = (uint32_t)xbox_InputConnectedMask() & 0xF;
        ins = now & ~reported;
        rem = reported & ~now;
        reported = now;
        if (ins | rem)
            fprintf(stderr, "[INPUT] XGetDeviceChanges: gamepad ports inserted 0x%X "
                    "removed 0x%X\n", ins, rem);
    }
    MEM32(p_ins) = ins;
    MEM32(p_rem) = rem;
    eax = (ins | rem) != 0;
    esp += 4 + 12;
}

/* HANDLE XInputOpen(PXPP_DEVICE_TYPE, DWORD dwPort, DWORD dwSlot,
 *                   PXINPUT_POLLING_PARAMETERS) -- ret 0x10 */
void sub_002051FF(void)
{
    uint32_t type = MEM32(esp + 4), port = MEM32(esp + 8), slot = MEM32(esp + 12);

    eax = 0;   /* NULL: no device there */
    if (type == BD_XDEVICE_TYPE_GAMEPAD && port < XBOX_MAX_CONTROLLERS
            && slot == 0 /* XDEVICE_NO_SLOT */
            && ((xbox_InputConnectedMask() >> port) & 1))
        eax = BD_PAD_HANDLE_BASE + port;
    fprintf(stderr, "[INPUT] XInputOpen(port %u) -> 0x%08X\n", port, eax);
    esp += 4 + 16;
}

/* VOID XInputClose(HANDLE) -- ret 4 */
void sub_00205255(void)
{
    esp += 4 + 4;
}

/* DWORD XInputGetCapabilities(HANDLE, PXINPUT_CAPABILITIES) -- ret 8 */
void sub_00205261(void)
{
    uint32_t handle = MEM32(esp + 4), p = MEM32(esp + 8), i;
    int port = bd_pad_port(handle);

    if (port < 0) {
        eax = BD_ERROR_INVALID_HANDLE;
    } else if (!((xbox_InputConnectedMask() >> port) & 1)) {
        eax = BD_ERROR_DEVICE_NOT_CONNECTED;
    } else {
        for (i = 0; i < 26; i++)
            MEM8(p + i) = 0;
        MEM8(p) = 1;                       /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
        MEM16(p + 4) = 0x00FF;             /* all eight digital buttons */
        for (i = 0; i < 8; i++)
            MEM8(p + 6 + i) = 0xFF;        /* 8-bit analog buttons */
        for (i = 0; i < 4; i++)
            MEM16(p + 14 + 2 * i) = 0xFFFF;  /* full-resolution sticks */
        MEM16(p + 22) = 0xFFFF;            /* both rumble motors */
        MEM16(p + 24) = 0xFFFF;
        eax = 0;
    }
    esp += 4 + 8;
}

/* DWORD XInputGetState(HANDLE, PXINPUT_STATE) -- ret 8 */
void sub_00205439(void)
{
    uint32_t handle = MEM32(esp + 4), p = MEM32(esp + 8), i;
    int port = bd_pad_port(handle);
    XBOX_INPUT_STATE state;

    if (port < 0) {
        eax = BD_ERROR_INVALID_HANDLE;
    } else if (xbox_InputGetState((DWORD)port, &state) != 0) {
        eax = BD_ERROR_DEVICE_NOT_CONNECTED;
    } else {
        MEM32(p) = state.dwPacketNumber;
        MEM16(p + 4) = state.Gamepad.wButtons;
        for (i = 0; i < 8; i++)
            MEM8(p + 6 + i) = state.Gamepad.bAnalogButtons[i];
        MEM16(p + 14) = (uint16_t)state.Gamepad.sThumbLX;
        MEM16(p + 16) = (uint16_t)state.Gamepad.sThumbLY;
        MEM16(p + 18) = (uint16_t)state.Gamepad.sThumbRX;
        MEM16(p + 20) = (uint16_t)state.Gamepad.sThumbRY;
        eax = 0;
    }
    esp += 4 + 8;
}

/* DWORD XInputSetState(HANDLE, PXINPUT_FEEDBACK) -- ret 8
 *
 * Asynchronous on the console: it returns ERROR_IO_PENDING and fills
 * Header.dwStatus in when the motors have been set. Here the motors are set at
 * once, so dwStatus reads done straight away; the pad layer only waits while
 * it reads ERROR_IO_PENDING. A caller waiting on Header.hEvent would need that
 * event signalled, and nothing here does -- Breakdown passes none. */
void sub_002054AC(void)
{
    uint32_t handle = MEM32(esp + 4), p = MEM32(esp + 8);
    int port = bd_pad_port(handle);

    if (port < 0) {
        eax = BD_ERROR_INVALID_HANDLE;
    } else {
        XBOX_VIBRATION vibration;
        static int warned_event;
        vibration.wLeftMotorSpeed = MEM16(p + 66);
        vibration.wRightMotorSpeed = MEM16(p + 68);
        xbox_InputSetState((DWORD)port, &vibration);   /* no-op without a pad */
        if (MEM32(p + 4) && !warned_event) {
            warned_event = 1;
            fprintf(stderr, "[INPUT] XInputSetState: feedback carries an event "
                    "handle this override never signals\n");
        }
        MEM32(p) = 0;                      /* Header.dwStatus = ERROR_SUCCESS */
        eax = BD_ERROR_IO_PENDING;
    }
    esp += 4 + 8;
}

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
/* Guest stack guard, checked on every indirect call.
 *
 * Play-test freezes (2026-09-15) came from the XAPI thread-notify list node
 * at 0x451778 in .data being overwritten with a stack frame: return address
 * 0x000219D0 (inside sub_000219A0), its argument, and a saved esi of 0x48 in
 * the routine slot. Some thread was running with esp inside .data. Every
 * guest stack lives in [XBOX_STACK_BASE, +XBOX_STACK_SIZE), so the first
 * call made from outside it is logged once with the frames pushed since. */
#define BD_GUEST_STACK_LO 0x00780000u
#define BD_GUEST_STACK_HI 0x00F80000u

static void bd_esp_guard(uint32_t va)
{
    static volatile LONG reported;
    /* esp 0: a host thread with no guest stack (the kernel bridge looking up
     * an ISR), not a guest call. */
    if (g_esp == 0 || (g_esp >= BD_GUEST_STACK_LO && g_esp < BD_GUEST_STACK_HI))
        return;
    if (InterlockedExchange(&reported, 1))
        return;
    fprintf(stderr, "[ESPGUARD] indirect call to 0x%08X with esp=%08X outside the guest "
            "stacks (tid %lu)\n", va, g_esp, (unsigned long)GetCurrentThreadId());
    fprintf(stderr, "  eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp(g)=%08X\n",
            g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp);
    for (int i = 0; i < 32; i += 8) {
        fprintf(stderr, "  [esp+%02X]", 4 * i);
        for (int j = 0; j < 8; j++)
            fprintf(stderr, " %08X", MEM32(g_esp + 4 * (i + j)));
        fputc('\n', stderr);
    }
    fprintf(stderr, "  recent ICALL targets (newest first):");
    for (int i = 1; i <= 16; i++)
        fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1)]);
    fputc('\n', stderr);
    fflush(stderr);
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    bd_esp_guard(xbox_va);
    /*
     * Defining a sub_XXXXXXXX here is only half of an override.
     * --exclude-manual keeps the lifter from emitting its own copy, which
     * also means the VA disappears from the GENERATED DISPATCH TABLE
     * (recomp_dispatch.c). Direct calls still link fine -- the linker
     * resolves the symbol -- but every INDIRECT call (RECOMP_ICALL, i.e.
     * vtable dispatch) goes through recomp_lookup(), which no longer knows
     * the address, and fails with "[ICALL] Failed to resolve VA ...",
     * silently returning eax = 0 instead of running the function.
     *
     * That bit us immediately: sub_001DA7DB is called both directly AND
     * through a vtable, and the first build of its override below fixed
     * the direct path while quietly breaking the indirect one. Anything
     * overridden here has to be re-registered in this function too.
     */
    if (xbox_va == 0x001DA7DB) return sub_001DA7DB;
    if (xbox_va == 0x001D3FD9) return sub_001D3FD9;
    /* XInput, above. The title calls these directly, but a dispatch entry
     * costs nothing and keeps an indirect call from failing silently. */
    if (xbox_va == 0x00204CDD) return sub_00204CDD;
    if (xbox_va == 0x002051FF) return sub_002051FF;
    if (xbox_va == 0x00205255) return sub_00205255;
    if (xbox_va == 0x00205261) return sub_00205261;
    if (xbox_va == 0x00205439) return sub_00205439;
    if (xbox_va == 0x002054AC) return sub_002054AC;

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
    static uint64_t fails;
    fails++;
    /* A null or garbage function pointer tends to be called in a loop; log
     * the first 20 in full, then every 100000th. */
    if (fails > 20 && (fails % 100000) != 0)
        return;
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu, failure #%llu)\n",
            va, (unsigned long long)g_icall_count, (unsigned long long)fails);
    /* Who called: RECOMP_ICALL has pushed the guest return address, so the
     * top of the guest stack names the call site; ecx is usually `this` for
     * a vtable call. */
    fprintf(stderr, "  eax=%08X ecx=%08X edx=%08X esi=%08X edi=%08X esp=%08X\n",
            g_eax, g_ecx, g_edx, g_esi, g_edi, g_esp);
    fprintf(stderr, "  guest stack:");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, " %08X", MEM32(g_esp + 4 * i));
    fputc('\n', stderr);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}
