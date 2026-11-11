"""patch_d3d11_omcache.py - idempotent. D3D11 backend: skip re-binding the same render target and depth state per draw.

d3d11_on_draw() called OMSetRenderTargets and OMSetDepthStencilState on every draw, even when the target, depth
buffer, depth/stencil state and stencil reference were the ones already bound. The backend's state cache (s_sc)
left them out on purpose, because other paths set them too. Here every other path marks them unknown after it
sets them (px debug clear, the clear/restore path, resize, present), and the draw skips the calls when nothing
changed. D3D11 holds a reference to a bound view, so a pointer cannot be reused for another view while it is bound.
S8 profile: the doorway's phase from OMSetRenderTargets to the draw call cost 2.49 ms of 6.76 ms.
RECOMP_D3D11_OM_CACHE=0 restores the old every-draw calls (A/B). Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[d3d11-omcache]'
s = io.open(P, encoding='utf-8', newline='').read()
if MARK in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'


def sub(a, b, what):
    global s
    n = s.count(a)
    if n != 1:
        print('REFUSED: anchor %r found %d times (want 1)' % (what, n)); raise SystemExit(1)
    s = s.replace(a, b)


a0 = 'static int s_sc_init;'
sub(a0, a0 + nl + nl.join([
    '/* ' + MARK + ' The output merger\'s target, depth view, depth/stencil state and stencil reference last bound by',
    ' * the draw path. Every other OMSet* site clears the matching "known" flag. RECOMP_D3D11_OM_CACHE=0: off. */',
    'static ID3D11RenderTargetView  *s_om_rtv;',
    'static ID3D11DepthStencilView  *s_om_dsv;',
    'static ID3D11DepthStencilState *s_om_dss;',
    'static UINT s_om_sref;',
    'static int s_om_known, s_dss_known, s_om_cache = -1;',
    'static int om_cache_on(void)',
    '{',
    '    if (s_om_cache < 0) {',
    '        const char *e = getenv("RECOMP_D3D11_OM_CACHE");',
    '        s_om_cache = !(e && *e == \'0\');',
    '    }',
    '    return s_om_cache;',
    '}']), 's_sc_init')

forget_rt = lambda ind: ind + 's_om_known = 0;   /* ' + MARK + ' */'
forget_ds = lambda ind: ind + 's_dss_known = 0;   /* ' + MARK + ' */'

a1 = '        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 8, rtv, dsv);'
sub(a1, a1 + nl + forget_rt('        '), 'px clear targets')
a2 = '        ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, dsv ? dss_le : s_ds_off, 0);'
sub(a2, a2 + nl + forget_ds('        '), 'px clear depth state')
a3 = '     * stays true for everything restored here */' + nl + '    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &rt->rtv, dsv);'
sub(a3, a3 + nl + forget_rt('    '), 'restore path targets')
a4 = '    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);'
sub(a4, a4 + nl + forget_rt('    '), 'resize unbind')
a5 = '    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &s_back_rtv, NULL);'
sub(a5, a5 + nl + forget_rt('    '), 'present targets')
a6 = '    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, s_ds_off, 0);'
sub(a6, a6 + nl + forget_ds('    '), 'present depth state')

a7 = ('        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &rt->rtv, dsv);' + nl
      + '        ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, dss,' + nl
      + '                                                   use_s ? v->stencil_ref & 0xFFu : 0);')
sub(a7, nl.join([
    '        {   /* ' + MARK + ' bind only what changed */',
    '            UINT sref_ = use_s ? v->stencil_ref & 0xFFu : 0;',
    '            int om_ = om_cache_on();',
    '            if (!om_ || !s_om_known || s_om_rtv != rt->rtv || s_om_dsv != dsv) {',
    '                ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &rt->rtv, dsv);',
    '                s_om_rtv = rt->rtv; s_om_dsv = dsv; s_om_known = 1;',
    '            }',
    '            if (!om_ || !s_dss_known || s_om_dss != dss || s_om_sref != sref_) {',
    '                ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, dss, sref_);',
    '                s_om_dss = dss; s_om_sref = sref_; s_dss_known = 1;',
    '            }',
    '        }']), 'draw OM binds')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
