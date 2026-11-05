"""patch_zonly.py - idempotent. D3D11 backend: support depth-only passes (colour surface offset 0, zeta set).
Xbox D3D allows SetRenderTarget(NULL, zbuffer) for z-only rendering (z pre-pass, shadow buffers). The
CPU rasteriser already writes depth with no colour surface; D3D11 returned no target, so those draws
AND their depth clears were skipped ('rt' in skip-why), and a later depth-EQUAL colour pass drew
nothing: Breakdown's rooms were black on D3D11 but fine on the CPU backend."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[zonly]' in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'

# 1. prototype + early return in surface()
old = ('static RenderTarget *surface(void)' + nl + '{' + nl +
       '    const struct nv2a_pb_gpu *g = nv2a_pb_gpu_state();' + nl +
       '    UINT w = g->clip_x + g->clip_w, h = g->clip_y + g->clip_h;' + nl +
       '    RenderTarget *free_rt = NULL, *lru = &s_rt[0];' + nl +
       '    int i;' + nl + nl +
       '    if (!g->color_offset || !w || !h)' + nl +
       '        return NULL;')
assert s.count(old) == 1, 'surface() head'
new = ('/* [zonly] depth-only passes: no colour target, defined after depth_target() */' + nl +
       'static RenderTarget *zonly_target(UINT w, UINT h);' + nl + nl +
       old.replace('    if (!g->color_offset || !w || !h)' + nl + '        return NULL;',
                   '    if (!g->color_offset && w && h && nv2a_pb_vsh_state()->zeta_offset)' + nl +
                   '        return zonly_target(w, h);' + nl +
                   '    if (!g->color_offset || !w || !h)' + nl + '        return NULL;'))
s = s.replace(old, new, 1)

# 2. the stand-in target, after depth_target()
anchor = '    fprintf(stderr, LOGP "zeta 0x%08X: %ux%u depth buffer\\n", zeta, w, h);' + nl + '    return e;' + nl + '}' + nl
assert s.count(anchor) == 1, 'depth_target end'
s = s.replace(anchor, anchor + nl +
    '/* [zonly] A stand-in target for a depth-only pass: sized like the depth buffer the colour pass at' + nl +
    ' * this zeta already uses (so both test against one buffer), else the clip. No texture, no view:' + nl +
    ' * OMSetRenderTargets with a NULL view binds depth only, which is what the hardware does. */' + nl +
    'static RenderTarget s_rt_zonly;' + nl +
    'static uint64_t s_zonly_draws;' + nl +
    'static RenderTarget *zonly_target(UINT w, UINT h)' + nl + '{' + nl +
    '    uint32_t zeta = nv2a_pb_vsh_state()->zeta_offset;' + nl +
    '    int i;' + nl +
    '    for (i = 0; i < DT_MAX; i++)' + nl +
    '        if (s_dt[i].tex && s_dt[i].zeta == zeta && s_dt[i].w >= w && s_dt[i].h >= h) {' + nl +
    '            w = s_dt[i].w; h = s_dt[i].h; break;' + nl +
    '        }' + nl +
    '    memset(&s_rt_zonly, 0, sizeof s_rt_zonly);' + nl +
    '    s_rt_zonly.w = w; s_rt_zonly.h = h;' + nl +
    '    nv2a_host_size(w, h, nv2a_host_render_scale(), D3D11_MAX_DIM, &s_rt_zonly.hw, &s_rt_zonly.hh);' + nl +
    '    if (s_zonly_draws++ == 0)' + nl +
    '        fprintf(stderr, LOGP "zeta 0x%08X: depth-only pass (no colour surface), %ux%u\\n", zeta, w, h);' + nl +
    '    return &s_rt_zonly;' + nl + '}' + nl, 1)

# 3. colour clear: nothing to clear without a colour view
old = ('    rt = surface();' + nl + '    if (!rt)' + nl + '        return;' + nl +
       '    c[0] = (float)((g->clear_color >> 16) & 0xFF) / 255.0f;')
assert s.count(old) == 1, 'colour clear'
s = s.replace(old, old.replace('    if (!rt)' + nl, '    if (!rt || !rt->rtv)   /* [zonly] */' + nl), 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
