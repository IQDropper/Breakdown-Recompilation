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
if '[guest-bounds]' in s:
    print('already patched')
    raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'


def rep(old, new):
    global s
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

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched: [guest-bounds] vertex, vertex array, texture, palette')
