"""patch_d3d_callback.py - toolkit fix (XBOXRECOMP_NOTES.md item 14): run D3DDevice_InsertCallback callbacks.

XDK D3DDevice_InsertCallback(type, fn, ctx) (read from Breakdown's own lifted 0x1C0890) pushes
    0x1D8C <- fn, 0x1D90 <- ctx            (ZSTENCIL/COLOR_CLEAR_VALUE used as mailboxes)
    NO_OPERATION 6  (READ callback)   or   WAIT_FOR_IDLE; NO_OPERATION 7  (WRITE callback)
On a console the GPU traps the software method and D3D's interrupt path calls fn(ctx) (cdecl).
The executor treated every software method but 1 (swap) as a no-op, so callbacks never ran.
Breakdown's Render_SubmitFrame inserts one and spins on the flag it clears (0x435800): frame 2, forever.

Fix: the executor queues (fn, ctx) on software method 6/7 (lock-free SPSC ring, executor thread -> ISR
thread); kernel_vblank_deliver(), which already runs the title's NV2A ISR on a guest stack ~60x/s,
drains it after the ISR, calling each fn(ctx) like kernel_run_dpc calls a DPC. Latency <= 1 vblank.
Needs RECOMP_VBLANK=1. Idempotent; refuses if the code moved."""
import pathlib, re
K = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\external\xboxrecomp\src\kernel')
pb, kb = K / 'nv2a_pb_exec.c', K / 'kernel_bridge.c'
MARK = '/* d3d_cb: see patch_d3d_callback.py */'
s = pb.read_text(encoding='utf-8')
if MARK in s:
    print('nv2a_pb_exec.c already patched')
else:
    ring = MARK + r'''
static uint32_t s_cb_fn, s_cb_ctx;                  /* last 0x1D8C / 0x1D90 written */
static uint32_t s_cb_q[16][2];
static volatile uint32_t s_cb_head, s_cb_tail;      /* producer: executor; consumer: vblank ISR */
static void d3d_cb_post(uint32_t fn, uint32_t ctx)
{
    uint32_t h = __atomic_load_n(&s_cb_head, __ATOMIC_RELAXED);
    if (h - __atomic_load_n(&s_cb_tail, __ATOMIC_ACQUIRE) >= 16u) {
        fprintf(stderr, "  [GPU] D3D callback queue full, dropping 0x%08X\n", fn);
        return;
    }
    s_cb_q[h & 15u][0] = fn; s_cb_q[h & 15u][1] = ctx;
    __atomic_store_n(&s_cb_head, h + 1u, __ATOMIC_RELEASE);
}
int xbox_Nv2aTakeD3dCallback(uint32_t *fn, uint32_t *ctx)
{
    uint32_t t = __atomic_load_n(&s_cb_tail, __ATOMIC_RELAXED);
    if (t == __atomic_load_n(&s_cb_head, __ATOMIC_ACQUIRE))
        return 0;
    *fn = s_cb_q[t & 15u][0]; *ctx = s_cb_q[t & 15u][1];
    __atomic_store_n(&s_cb_tail, t + 1u, __ATOMIC_RELEASE);
    return 1;
}

'''
    anchor = 'static uint32_t dma_resolve(uint32_t offset)'
    nop_old = re.search(r'(case NV097_NO_OPERATION:.*?)(\n\s*if \(param\)\n\s*xbox_FlipHoldSwap\(&s_flip_hold, param\);)', s, re.S)
    i = s.find('static int shown[8];'); j = s.rfind('if (verbose) {', 0, i)
    assert s.count(anchor) == 1 and nop_old and i > 0 and 0 < i - j < 80, 'toolkit changed - patch by hand'
    s = s[:j] + 'if (method == 0x1D8Cu) s_cb_fn = param;       /* d3d_cb */\n    else if (method == 0x1D90u) s_cb_ctx = param;\n    ' + s[j:]
    s = s.replace(nop_old.group(2), nop_old.group(2) +
                  '\n        if ((param & 0x1Fu) == 6u || (param & 0x1Fu) == 7u)   /* d3d_cb: InsertCallback READ/WRITE */\n'
                  '            d3d_cb_post(s_cb_fn, s_cb_ctx);', 1)
    s = s.replace(anchor, ring + anchor, 1)
    pb.write_text(s, encoding='utf-8'); print('patched nv2a_pb_exec.c')
k = kb.read_text(encoding='utf-8')
if MARK in k:
    print('kernel_bridge.c already patched')
else:
    old = '    int claimed = kernel_call_isr(NV2A_VECTOR);\n'
    assert k.count(old) == 1, 'toolkit changed - patch by hand'
    drain = old + '    ' + MARK + r'''
    {   /* D3D callbacks the executor reached since the last vblank: fn(ctx), cdecl. */
        extern int xbox_Nv2aTakeD3dCallback(uint32_t *, uint32_t *);
        static unsigned shown;
        uint32_t cb, ctx;
        while (xbox_Nv2aTakeD3dCallback(&cb, &ctx)) {
            recomp_func_t fn = recomp_lookup(cb);
            if (!fn) fn = recomp_lookup_manual(cb);
            if (shown++ < 4) {
                fprintf(stderr, "  [NV2A] D3D callback 0x%08X(ctx 0x%08X)%s\n", cb, ctx, fn ? "" : " NOT IN DISPATCH");
                fflush(stderr);
            }
            if (!fn) continue;
            g_esp -= 4; BRIDGE_MEM32(g_esp) = ctx;
            g_esp -= 4; BRIDGE_MEM32(g_esp) = 0;      /* dummy return address; fn's ret pops it */
            fn();
            g_esp += 4;                                /* cdecl: caller pops ctx */
        }
    }
'''
    kb.write_text(k.replace(old, drain, 1), encoding='utf-8'); print('patched kernel_bridge.c')
