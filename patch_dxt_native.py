"""patch_dxt_native.py - idempotent. D3D11 backend: upload the game's DXT1/DXT3/DXT5 textures to the GPU as
BC1/BC2/BC3 instead of decoding them to A8R8G8B8 on the CPU.

Breakdown 2026-11-14 (P6.3c, map viewer 7F): the CPU decoder decodes a whole 4x4 block for every texel, so the first
scene frame after a load spent 27-43 ms on textures. With native upload: 16 ms. The GPU decodes the same blocks, so
the picture is the same. Kept on the CPU path: sizes that are not multiples of 4, swizzled/linear colour formats, and
texture dumping (RECOMP_DEBUG texdump needs the decoded pixels). RECOMP_DXT_NATIVE=0 turns it off for A/B tests.
Needs patch_guest_bounds.py first (guest_readable)."""
import io
import re

P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
BS = chr(92)
s = io.open(P, encoding='utf-8', newline='').read()
OLD_COND = '    if (!src && dxt_native_on() && !s_texdump_dir && '
GUARD = '#ifdef TEXDUMP_CAP   /* [dxt-native] RECOMP_TEX_DUMP needs the decoded pixels (patch_tex_dump.py is optional) */\n#define DXT_NATIVE_DUMPING (s_texdump_dir != NULL)\n#else\n#define DXT_NATIVE_DUMPING 0\n#endif\n'
if '[dxt-native]' in s:
    if OLD_COND in s and 'DXT_NATIVE_DUMPING' not in s:
        # earlier version: referred to the optional texture dumper directly, so it only built with patch_tex_dump.py
        nl_ = '\r\n' if '\r\n' in s else '\n'
        s = s.replace(OLD_COND, GUARD.replace('\n', nl_) + '    if (!src && dxt_native_on() && !DXT_NATIVE_DUMPING && ', 1)
        io.open(P, 'w', encoding='utf-8', newline='').write(s)
        print('upgraded: [dxt-native] builds without the texture dumper')
    else:
        print('already patched')
    raise SystemExit
assert 'static int guest_readable(' in s, 'run patch_guest_bounds.py first'
nl = '\r\n' if '\r\n' in s else '\n'


def rep(old, new):
    global s
    old = old.replace('\n', nl)
    new = new.replace('\n', nl)
    assert s.count(old) == 1, 'site not found: ' + old[:60]
    s = s.replace(old, new, 1)


# tex_alloc: its 'bc3' flag becomes a block format: 0 A8R8G8B8, 1 BC3 (HD packs), 2 BC1, 3 BC2
rep('    td.Format = bc3 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;',
    '    /* [dxt-native] bc: 0 A8R8G8B8, 1 BC3 (HD packs, DXT5), 2 BC1 (DXT1), 3 BC2 (DXT3) */\n'
    '    td.Format = bc3 == 1 ? DXGI_FORMAT_BC3_UNORM : bc3 == 2 ? DXGI_FORMAT_BC1_UNORM\n'
    '              : bc3 == 3 ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;')

# BC1 rows are 8-byte blocks
rep('''        uint32_t row = bc3 ? (w + 3u) / 4u * 16u : w * 4u;''',
    '''        uint32_t row = bc3 ? (w + 3u) / 4u * (bc3 == 2 ? 8u : 16u) : w * 4u;   /* [dxt-native] BC1 = 8-byte blocks */''')

# the switch, before tex_upload
rep('static int tex_upload(TexEntry *e, const Stage *t, uint64_t hash)\n{',
    '/* [dxt-native] RECOMP_DXT_NATIVE=0 decodes DXT on the CPU as before (A/B). */\n'
    'static uint64_t s_dxt_native;\n'
    'static int dxt_native_on(void)\n{\n    static int on = -1;\n    if (on < 0) {\n'
    '        const char *e = getenv("RECOMP_DXT_NATIVE");\n        on = !(e && e[0] == \'0\');\n'
    '        fprintf(stderr, "[dxt-native] %s' + BS + 'n", on ? "on: DXT textures go to the GPU as BC1/BC2/BC3" : "off");\n'
    '    }\n    return on;\n}\n\n'
    'static int tex_upload(TexEntry *e, const Stage *t, uint64_t hash)\n{')

# tex_upload: the game's own DXT goes up as it is, before the CPU decode path ('if (!src) {' that decodes;
# patch_d3d11_prof-style timers may sit inside it, so match on the decode call within the next lines)
m = None
for m_ in re.finditer(re.escape('    if (!src) {' + nl), s):
    if 'decode_texture(t)' in s[m_.end():m_.end() + 200]:
        assert m is None, 'two decode sites'
        m = m_
assert m, 'decode site not found'
ins = (GUARD + '''    if (!src && dxt_native_on() && !DXT_NATIVE_DUMPING && !((t->width | t->height) & 3u)
            && (t->color == 0x0C || t->color == 0x0E || t->color == 0x0F)) {   /* [dxt-native] */
        int bc = t->color == 0x0C ? 2 : t->color == 0x0E ? 3 : 1;
        size_t need = 0;
        for (l = 0; l < levels; l++)
            need += (size_t)((nv2a_tex_level_dim(width, l) + 3u) / 4u) * ((nv2a_tex_level_dim(height, l) + 3u) / 4u)
                  * (bc == 2 ? 8u : 16u);
        if (guest_readable(t->offset, need, "dxt texture") && tex_alloc(e, width, height, levels, bc)) {
            src = (const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + t->offset);
            bc3 = bc;
            s_dxt_native++;
        }
    }
''').replace('\n', nl)
s = s[:m.start()] + ins + s[m.start():]

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched: [dxt-native] BC1/BC2/BC3 upload')
