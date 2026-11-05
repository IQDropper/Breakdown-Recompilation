"""patch_rtt_sub.py - idempotent. D3D11 render-to-texture: a linear texture that starts INSIDE a render
target (same pitch, e.g. 16 rows down because the visible clip starts at y=16) now binds a GPU copy of
that sub-rectangle. Before: only a texture starting exactly at the target's address matched; anything
else decoded guest memory, which this backend never writes, so it read zeros. Breakdown's menu overlay
(and likely the in-game screen effects) samples the frame that way and painted transparent black."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[rtt-sub]' in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'
def L(*lines): return nl.join(lines) + nl

# 1. helper before rt_texture()
anchor = 'static RenderTarget *rt_texture(const Stage *t)' + nl
assert s.count(anchor) == 1
helper = L(
'/* [rtt-sub] A texture inside a target: copy the sub-rectangle it covers. */',
'static RenderTarget     s_sub;',
'static uint64_t         s_sub_copies;',
'static RenderTarget *rt_sub_copy(RenderTarget *rt, const Stage *t, uint32_t delta)',
'{',
'    UINT row = delta / rt->pitch, colb = delta % rt->pitch, col, gw, gh;',
'    D3D11_BOX box;',
'    UINT bw, bh;',
'    if (!rt->bpp || colb % rt->bpp)',
'        return NULL;',
'    col = colb / rt->bpp;',
'    if (row >= rt->h || col >= rt->w || !t->width || !t->height)',
'        return NULL;',
'    gw = t->width;  if (col + gw > rt->w) gw = rt->w - col;',
'    gh = t->height; if (row + gh > rt->h) gh = rt->h - row;',
'    box.left = col * rt->hw / rt->w;          box.right = (col + gw) * rt->hw / rt->w;',
'    box.top = row * rt->hh / rt->h;           box.bottom = (row + gh) * rt->hh / rt->h;',
'    box.front = 0; box.back = 1;',
'    bw = box.right - box.left; bh = box.bottom - box.top;',
'    if (!bw || !bh)',
'        return NULL;',
'    if (s_sub.tex && (s_sub.hw != bw || s_sub.hh != bh)) {',
'        if (s_sub.srv) ID3D11ShaderResourceView_Release(s_sub.srv);',
'        ID3D11Texture2D_Release(s_sub.tex);',
'        s_sub.tex = NULL; s_sub.srv = NULL;',
'    }',
'    if (!s_sub.tex) {',
'        D3D11_TEXTURE2D_DESC td;',
'        ID3D11Texture2D_GetDesc(rt->tex, &td);',
'        td.Width = bw; td.Height = bh;',
'        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;',
'        td.MiscFlags = 0;',
'        if (FAILED(ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s_sub.tex)))',
'            return NULL;',
'        if (FAILED(ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)s_sub.tex, NULL, &s_sub.srv))) {',
'            ID3D11Texture2D_Release(s_sub.tex); s_sub.tex = NULL;',
'            return NULL;',
'        }',
'        s_sub.hw = bw; s_sub.hh = bh;',
'    }',
'    ID3D11DeviceContext_CopySubresourceRegion(s_ctx, (ID3D11Resource *)s_sub.tex, 0, 0, 0, 0,',
'                                              (ID3D11Resource *)rt->tex, 0, &box);',
'    s_sub.w = gw; s_sub.h = gh;',
'    s_sub.offset = rt->offset; s_sub.addr = rt->addr + delta;',
'    s_sub.pitch = rt->pitch; s_sub.bpp = rt->bpp;',
'    if (s_sub_copies++ < 4)',
'        fprintf(stderr, LOGP "texture 0x%08X is %ux%u at (%u,%u) inside surface 0x%08X: bound as a copy\\n",',
'                t->offset, gw, gh, col, row, rt->offset);',
'    return &s_sub;',
'}',
'')
s = s.replace(anchor, helper + anchor, 1)

# 2. remember a containing target in the loop, use it after
old = L('        /* Low 27 bits: the same memory through either window. */',
        '        if (!rt->tex || ((rt->addr ^ t->offset) & 0x07FFFFFFu))',
        '            continue;')
assert s.count(old) == 1, 'loop head'
new = L('        /* Low 27 bits: the same memory through either window. */',
        '        if (rt->tex && ((rt->addr ^ t->offset) & 0x07FFFFFFu) && !sub_rt',
        '                && !d3d8_format_is_swizzled(t->color) && t->pitch == rt->pitch',
        '                && rt->bpp == nv2a_tex_texel_bytes(lin)) {   /* [rtt-sub] */',
        '            uint32_t dl = ((t->offset & 0x07FFFFFFu) - (rt->addr & 0x07FFFFFFu)) & 0x07FFFFFFu;',
        '            if (dl < rt->pitch * rt->h && !rt_drop_if_stale(rt, "bind")) { sub_rt = rt; sub_delta = dl; }',
        '        }',
        '        if (!rt->tex || ((rt->addr ^ t->offset) & 0x07FFFFFFu))',
        '            continue;')
s = s.replace(old, new, 1)
old = '    RenderTarget *self = NULL;' + nl + '    uint32_t lin = nv2a_tex_linear_twin(t->color);'
assert s.count(old) == 1, 'decls'
s = s.replace(old, '    RenderTarget *self = NULL, *sub_rt = NULL;   /* [rtt-sub] */' + nl +
              '    uint32_t sub_delta = 0;' + nl + '    uint32_t lin = nv2a_tex_linear_twin(t->color);', 1)
old = '    return self ? rt_self_copy(self) : NULL;' + nl + '}'
assert s.count(old) == 1, 'tail'
s = s.replace(old, '    if (self)' + nl + '        return rt_self_copy(self);' + nl +
              '    return sub_rt ? rt_sub_copy(sub_rt, t, sub_delta) : NULL;   /* [rtt-sub] */' + nl + '}', 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
