"""patch_sema_dma.py - toolkit fix (XBOXRECOMP_NOTES.md item 13): semaphore releases ignore the DMA object's base.

nv2a_pb_exec.c resolved NV097_BACK_END_WRITE_SEMAPHORE_RELEASE as dma_resolve(offset), i.e. it assumed the
semaphore context DMA covers physical memory from 0 (its own comment says so, and says to log when a title
proves otherwise). Breakdown's D3D (XDK 5659) points that DMA object at physical 0xF000 and polls its GPU
time at 0x8000F000, while the releases landed at 0x80000000 -> D3D_BlockOnTime spins forever (black screen).

Fix: read the DMA object the title built. D3D writes PRAMIN/RAMHT through the NV2A aperture, which this
runtime maps as plain memory, so the bytes are there:
  NV_PFIFO_RAMHT (0xFD002210): base = bits 8:4 << 12, size = 4 KB << bits 17:16
  RAMHT entry (8 bytes): handle, context (bit 31 valid, bits 15:0 = instance >> 4)
  DMA object @ 0xFD700000 + instance: w0 bits 31:20 = adjust, w2 bits 31:12 = page frame
  base = (w2 & 0xFFFFF000) | (w0 >> 20)
The RAMHT is scanned linearly for the handle (no hash needed). If nothing valid is found the old behaviour
(base 0) stays, and both outcomes are logged once. Idempotent; refuses if the code moved."""
import pathlib
p = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\external\xboxrecomp\src\kernel\nv2a_pb_exec.c')
s = p.read_text(encoding='utf-8')
MARK = '/* sema_dma_base: see patch_sema_dma.py */'
helper = MARK + r'''
static uint32_t sema_dma_base(uint32_t handle)
{
    static uint32_t cached_handle = 0xFFFFFFFFu, cached_base;
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t ramht, base, size, i;

    if (handle == cached_handle)
        return cached_base;
    cached_handle = handle;
    cached_base = 0;
    ramht = *(const volatile uint32_t *)(mem + 0xFD002210u);       /* NV_PFIFO_RAMHT */
    base = ((ramht >> 4) & 0x1Fu) << 12;
    size = 0x1000u << ((ramht >> 16) & 3u);
    for (i = 0; i < size; i += 8) {
        uint32_t h = *(const volatile uint32_t *)(mem + 0xFD700000u + base + i);
        uint32_t ctx = *(const volatile uint32_t *)(mem + 0xFD700000u + base + i + 4);
        if (h == handle && (ctx & 0x80000000u)) {
            uint32_t inst = (ctx & 0xFFFFu) << 4;
            const volatile uint32_t *o = (const volatile uint32_t *)(mem + 0xFD700000u + inst);
            uint32_t b = (o[2] & 0xFFFFF000u) | (o[0] >> 20);
            fprintf(stderr, "  [GPU] semaphore DMA 0x%08X: RAMHT 0x%08X entry %u -> inst 0x%05X"
                    " obj %08X %08X %08X -> base 0x%08X\n", handle, ramht, i / 8, inst, o[0], o[1], o[2], b);
            if (b < 0x08000000u)          /* inside 128 MB of RAM: believe it */
                cached_base = b;
            return cached_base;
        }
    }
    fprintf(stderr, "  [GPU] semaphore DMA 0x%08X: not in RAMHT (reg 0x%08X); assuming base 0\n", handle, ramht);
    return 0;
}

'''
anchor = 'static uint32_t dma_resolve(uint32_t offset)'
old = 'uint32_t va = dma_resolve(s_gpu.sema_offset);'
new = 'uint32_t va = dma_resolve(sema_dma_base(s_gpu.sema_dma) + s_gpu.sema_offset);'
if MARK in s:
    print('already patched')
else:
    assert s.count(anchor) == 1 and s.count(old) == 1, 'toolkit changed - patch by hand'
    s = s.replace(anchor, helper + anchor).replace(old, new)
    p.write_text(s, encoding='utf-8')
    print('patched nv2a_pb_exec.c: semaphore release uses the DMA object base')
