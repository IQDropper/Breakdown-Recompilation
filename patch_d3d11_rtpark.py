"""patch_d3d11_rtpark.py - idempotent. D3D11 backend: reuse retired render targets instead of re-creating them.

Breakdown draws a chain of small targets in the same memory every frame in busy rooms (0x01182000: 256x256,
256x128, 128x64, ...; a downsample/blur chain). The backend keeps no two live targets on the same memory, so each new
one retired the one it overlapped (rt_release) and the next frame created it again: CreateTexture2D +
CreateRenderTargetView + a log line, ~5 times a frame (S8 doorway run: 5240 creations; surface() 1.1 ms of the
6.7 ms draw path).

Now a retired target's texture and views are parked (up to RT_PARK_MAX, oldest released first) and a new target of
the same host size takes a parked one instead of creating it. A new target's contents were undefined before
(CreateTexture2D with no initial data) and still are, so drawing is unchanged. The ownership hash and the rest of
creation still run. Only real creations are logged. RECOMP_D3D11_RT_PARK=0: old behaviour (A/B).
Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[d3d11-rtpark]'
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


a1 = '/* ---- render-target ownership (nv2a_backend_common.h) ----'
sub(a1, nl.join([
    '/* ' + MARK + ' Retired targets\' textures and views, kept for the next target of the same host size. */',
    '#define RT_PARK_MAX 8',
    'static struct {',
    '    ID3D11Texture2D          *tex;',
    '    ID3D11RenderTargetView   *rtv;',
    '    ID3D11ShaderResourceView *srv;',
    '    UINT hw, hh;',
    '    uint64_t when;',
    '} s_park[RT_PARK_MAX];',
    'static uint64_t s_park_seq, s_park_reused;',
    'static int s_park_hit;',
    'static int park_on(void)',
    '{',
    '    static int v = -1;',
    '    if (v < 0) {',
    '        const char *e = getenv("RECOMP_D3D11_RT_PARK");',
    '        v = !(e && *e == \'0\');',
    '    }',
    '    return v;',
    '}',
    '/* Like rt_release, but the texture and its views wait in s_park. */',
    'static void rt_park(RenderTarget *rt)',
    '{',
    '    int i, slot = 0;',
    '    if (!park_on() || !rt->tex || !rt->rtv) {',
    '        rt_release(rt);',
    '        return;',
    '    }',
    '    if (s_drawn == rt)',
    '        s_drawn = NULL;',
    '    for (i = 0; i < RT_PARK_MAX; i++) {',
    '        if (!s_park[i].tex) { slot = i; break; }',
    '        if (s_park[i].when < s_park[slot].when) slot = i;',
    '    }',
    '    if (s_park[slot].tex) {',
    '        if (s_park[slot].srv) ID3D11ShaderResourceView_Release(s_park[slot].srv);',
    '        ID3D11RenderTargetView_Release(s_park[slot].rtv);',
    '        ID3D11Texture2D_Release(s_park[slot].tex);',
    '    }',
    '    s_park[slot].tex = rt->tex; s_park[slot].rtv = rt->rtv; s_park[slot].srv = rt->srv;',
    '    s_park[slot].hw = rt->hw; s_park[slot].hh = rt->hh; s_park[slot].when = ++s_park_seq;',
    '    memset(rt, 0, sizeof *rt);',
    '}',
    '/* A parked texture of rt\'s host size into rt (hw, hh set): 1, or 0 when there is none. */',
    'static int rt_unpark(RenderTarget *rt)',
    '{',
    '    int i;',
    '    if (!park_on())',
    '        return 0;',
    '    for (i = 0; i < RT_PARK_MAX; i++)',
    '        if (s_park[i].tex && s_park[i].hw == rt->hw && s_park[i].hh == rt->hh) {',
    '            rt->tex = s_park[i].tex; rt->rtv = s_park[i].rtv; rt->srv = s_park[i].srv;',
    '            memset(&s_park[i], 0, sizeof s_park[i]);',
    '            if (s_park_reused++ == 0)',
    '                fprintf(stderr, LOGP "render targets: retired ones are reused (RECOMP_D3D11_RT_PARK=0 to re-create)\\n");',
    '            s_park_hit = 1;',
    '            return 1;',
    '        }',
    '    return 0;',
    '}',
    '',
    a1]), 'ownership section header')

a2 = ('                            g->color_offset, w, h);' + nl
      + '                rt_release(rt);')
sub(a2, ('                            g->color_offset, w, h);' + nl
         + '                rt_park(rt);   /* ' + MARK + ' */'), 'alias retire')

a3 = ('    {' + nl + '        D3D11_TEXTURE2D_DESC td;' + nl + '        memset(&td, 0, sizeof td);' + nl
      + '        td.Width = free_rt->hw;')
sub(a3, '    if (!rt_unpark(free_rt))   /* ' + MARK + ' */' + nl + a3, 'target creation block')

a4 = ('    if (free_rt->hw != w)' + nl
      + '        fprintf(stderr, LOGP "surface 0x%08X: %ux%u render target (%ux%u host)\\n",')
sub(a4, ('    if (s_park_hit)   /* ' + MARK + ' reused, not created: no line */' + nl
         + '        s_park_hit = 0;' + nl
         + '    else if (free_rt->hw != w)' + nl
         + '        fprintf(stderr, LOGP "surface 0x%08X: %ux%u render target (%ux%u host)\\n",'), 'creation log')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
