"""patch_guest_bounds.py - idempotent. D3D11 backend: check that a guest address is mapped before reading vertex,
texture or palette data from it.

Breakdown 2026-11-14 (map viewer walk, Shaft D): a host-thread access violation reading guest 0xEA0B0250, outside
RAM and its mirrors, inside a system DLL (memcpy), with no game thread involved. The renderer stopped and the window
stayed white while the game kept running. Reads below 64 MB (main RAM, always mapped) skip the check; above it,
VirtualQuery decides. An unreadable range skips that draw (s_skip_why[8]) or that texture, and is logged with its
draw state the first 8 times as [guest-bounds]."""
import io

P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
BASE_DONE = '[guest-bounds]' in s


def rep(old, new, stage_done=None):
    global s
    if stage_done if stage_done is not None else BASE_DONE:
        return
    old = old.replace('\n', nl)
    new = new.replace('\n', nl)
    assert s.count(old) == 1, 'site not found: ' + old[:60]
    s = s.replace(old, new, 1)


rep('''/* ---- vertices -------------------------------------------------------------- */
''', '''/* ---- vertices -------------------------------------------------------------- */

/* [guest-bounds] 1 when the host can read n bytes at guest address va (main RAM, or a mapped mirror/view above it).
 * A garbage vertex/texture address read the unmapped part of the guest reservation from this thread and stopped the
 * renderer (white window, game still running). Below 64 MB: always mapped, no call. */
static uint64_t s_guest_bad;
static int guest_readable(uint32_t va, size_t n, const char *what)
{
    const uint8_t *p, *e;
    MEMORY_BASIC_INFORMATION mbi;
    if ((uint64_t)va + n <= 0x04000000u)
        return 1;
    p = (const uint8_t *)xbox_GetMemoryOffset() + va;
    e = p + n;
    while (p < e) {
        if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT
                || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect)
            goto bad;
        p = (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
bad:
    if (s_guest_bad++ < 8) {
        fprintf(stderr, "[guest-bounds] %s: %zu bytes at guest 0x%08X are not mapped; skipped\\n", what, n, va);
        fflush(stderr);
    }
    return 0;
}
''')

rep('''        p = (const uint8_t *)xbox_GetMemoryOffset() + a->offset
          + (size_t)index * a->stride;
''', '''        if (!guest_readable((uint32_t)(a->offset + (size_t)index * a->stride), 16, "vertex"))
            return 0;
        p = (const uint8_t *)xbox_GetMemoryOffset() + a->offset
          + (size_t)index * a->stride;
''')

rep('''                uint8_t *dst = (uint8_t *)vb_alloc(bytes, &at);
                if (!dst) {''', '''                uint8_t *dst;
                if (!d->inline_data && !guest_readable((uint32_t)(lo_off + (size_t)lo * a->stride), bytes,
                                                       "vertex array")) {
                    vb_end();
                    s_draws_skipped++; s_skip_why[8]++;
                    return;
                }
                dst = (uint8_t *)vb_alloc(bytes, &at);
                if (!dst) {''')

rep('''    if (t->color == NV2A_TEX_P8)
        pal = (const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset()
                                 + t->palette);''', '''    if (!guest_readable(t->offset, n * 4, "texture"))
        return 0;
    if (t->color == NV2A_TEX_P8) {
        if (!guest_readable(t->palette, (size_t)t->pal_len * 4, "palette"))
            return 0;
        pal = (const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset()
                                 + t->palette);
    }''')

# [guest-bounds-cache] P6.3b (2026-11-14): one VirtualQuery per draw cost 90-130 ms a frame in 7F (the walking
# stutter after #60). Remember the last 16 regions found readable; guest RAM views stay mapped while the game runs.
CACHE_DONE = '[guest-bounds-cache]' in s
rep('''static uint64_t s_guest_bad;
static int guest_readable(uint32_t va, size_t n, const char *what)
{''', '''static uint64_t s_guest_bad;
static const uint8_t *s_gr_lo[16], *s_gr_hi[16];   /* [guest-bounds-cache] confirmed readable host ranges */
static int s_gr_next;
static int guest_readable(uint32_t va, size_t n, const char *what)
{''', CACHE_DONE)
rep('''    e = p + n;
    while (p < e) {
        if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT
                || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect)
            goto bad;
        p = (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;''', '''    e = p + n;
    {   /* [guest-bounds-cache] a range inside a region already found readable: no VirtualQuery (it cost 90-130 ms
         * a frame in 7F, P6.3b). Guest RAM views are mapped once and not unmapped while the game runs. */
        int c;
        for (c = 0; c < 16; c++)
            if (p >= s_gr_lo[c] && e <= s_gr_hi[c])
                return 1;
    }
    while (p < e) {
        if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT
                || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect)
            goto bad;
        s_gr_lo[s_gr_next] = (const uint8_t *)mbi.BaseAddress;
        s_gr_hi[s_gr_next] = (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;
        s_gr_next = (s_gr_next + 1) & 15;
        p = (const uint8_t *)mbi.BaseAddress + mbi.RegionSize;''', CACHE_DONE)

if BASE_DONE and CACHE_DONE:
    print('already patched')
else:
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
    print('patched:' + ('' if BASE_DONE else ' [guest-bounds] vertex, vertex array, texture, palette;')
          + ('' if CACHE_DONE else ' [guest-bounds-cache]'))
