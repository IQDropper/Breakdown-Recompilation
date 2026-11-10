"""patch_input_bridge.py - connect your real controller to Breakdown's own (statically linked) XAPI input library.

Why: on a console XInputOpen/XInputGetState talk to the USB pad through XAPI's USB stack. The native build has
no emulated USB pad running (0 OHCI interrupts in the log), so XGetDeviceChanges never reports a pad and the
game, which polls every frame (Input_PollGamepads 0x18c5a0, gate 0x434EDC = 1), never opens one.
How: recomp_manual.c overrides the six XAPI calls Input_PollGamepads makes (all __stdcall), mapping them to
the toolkit's host input layer (xbox_Input*: XInput / SDL pads, + keyboard with RECOMP_KEYBOARD=1).
The recompiler sees each `void sub_XXXXXXXX(void)` definition and stops generating its own (manual_scan.py).
Idempotent; refuses if recomp_manual.c moved."""
import pathlib
P = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\src\recomp_manual.c')
s = P.read_text(encoding='utf-8')
MARK = '/* == Input bridge (patch_input_bridge.py) == */'
if MARK in s:
    print('already patched'); raise SystemExit
inc_old = '#include <stddef.h>   /* ptrdiff_t */\n'
eax_old = 'extern uint32_t g_eax;\n'
assert s.count(inc_old) == 1 and s.count(eax_old) == 1, 'recomp_manual.c changed - patch by hand'
s = s.replace(inc_old, inc_old + '#include "gen/recomp_types.h"   /* g_esp/g_eax (thread-local) + MEM32 - for the input bridge */\n#include "pc_options_host.h"\n')
s = s.replace(eax_old, '/* g_eax: declared thread-local by recomp_types.h (a plain extern here would name the wrong copy) */\n')
s += '\n' + MARK + r'''
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
        /* RECOMP_KEYBOARD=1: port 0 answers with no pad plugged in; report it so a keyboard-only player gets a pad. */
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
'''
P.write_text(s, encoding='utf-8')
print('patched recomp_manual.c: 6 XAPI input overrides')
