"""patch_poly_offset.py - idempotent. D3D11 backend: apply the game's NV2A polygon offset.

Breakdown draws shadow decals on the floor with polygon offset (SET_POLY_OFFSET_FILL_ENABLE 0x338,
SET_POLYGON_OFFSET_SCALE_FACTOR 0x384, SET_POLYGON_OFFSET_BIAS 0x388). The toolkit decodes them into
s_vsh.poly_offset / poly_factor / poly_bias (nv2a_pb_exec.c), but the D3D11 backend's fixed
rasteriser states had DepthBias 0, so the decals z-fought the floor and strobed.

Fix: rasterizer_state() hands back a state with DepthBias / SlopeScaledDepthBias when the draw has
polygon offset on. States are cached by (cull, front, scissor, bias, factor). Mapping is GL's
glPolygonOffset(factor, units), which is what the NV2A registers mean and what xemu passes through:
  SlopeScaledDepthBias = poly_factor
  DepthBias            = round(poly_bias * 16777215 / zmax)   (zmax 16777215 for Z24, 65535 for Z16)
The depth buffer is D32_FLOAT where one D3D11 unit is 2^(exponent(maxz) - 23), i.e. 2^-24 for z in
[0.5, 1), matching one Z24 step. Env RECOMP_POLY_OFFSET=0 turns it off (A/B). Each distinct
(factor, bias) pair is logged once, then again at 10/100/1000... draws.
Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[poly-offset]'
s = io.open(P, encoding='utf-8', newline='').read()
if MARK in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'

def once(a, what):
    n = s.count(a)
    if n != 1:
        print('REFUSED: anchor %r found %d times (want 1)' % (what, n)); raise SystemExit(1)

# 1. the cached-state helper, ahead of rasterizer_state()
a1 = '/* The state for the pgraph cull setting: CULL_FACE 0x404 FRONT, 0x405 BACK,'
once(a1, 'rasterizer_state comment')
helper = nl.join([
'/* ' + MARK + ' Rasteriser states with the game\'s polygon offset, cached by',
' * (cull, front, scissor, bias, factor). NV2A glPolygonOffset(factor, units) maps to',
' * SlopeScaledDepthBias = factor and DepthBias = units scaled to Z24 steps. D3D11 float depth',
' * resolves 2^(e-23) per unit (e = exponent of the primitive\'s max z), 2^-24 for z in [0.5,1). */',
'#define PO_CACHE 32',
'static struct { int c, f, sc; INT bias; float slope; ID3D11RasterizerState *rs; unsigned long long n, next; } s_po[PO_CACHE];',
'static int s_po_n;',
'static ID3D11RasterizerState *poly_offset_state(ID3D11RasterizerState *plain, int c, int f)',
'{',
'    const struct nv2a_pb_vsh *v = nv2a_pb_vsh_state();',
'    static int on = -1;',
'    INT bias;',
'    float slope, zmax;',
'    int i;',
'    if (on < 0) {',
'        const char *e = getenv("RECOMP_POLY_OFFSET");',
'        on = !(e && e[0] == \'0\');',
'    }',
'    if (!on || !v->poly_offset)',
'        return plain;',
'    zmax = nv2a_zmax(nv2a_pb_gpu_state()->format);',
'    slope = v->poly_factor;',
'    bias = (INT)(v->poly_bias * (16777215.0f / zmax) + (v->poly_bias < 0 ? -0.5f : 0.5f));',
'    for (i = 0; i < s_po_n; i++)',
'        if (s_po[i].c == c && s_po[i].f == f && s_po[i].sc == 1 && s_po[i].bias == bias && s_po[i].slope == slope)',
'            break;',
'    if (i == s_po_n) {',
'        D3D11_RASTERIZER_DESC rd;',
'        ID3D11RasterizerState *rs = NULL;',
'        if (s_po_n >= PO_CACHE)',
'            return plain;',
'        memset(&rd, 0, sizeof rd);',
'        rd.FillMode = D3D11_FILL_SOLID;',
'        rd.CullMode = c == 0 ? D3D11_CULL_NONE : c == 1 ? D3D11_CULL_BACK : D3D11_CULL_FRONT;',
'        rd.FrontCounterClockwise = f;',
'        rd.DepthClipEnable = TRUE;',
'        rd.ScissorEnable = TRUE;',
'        rd.DepthBias = bias;',
'        rd.SlopeScaledDepthBias = slope;',
'        if (FAILED(ID3D11Device_CreateRasterizerState(s_dev, &rd, &rs)) || !rs)',
'            return plain;',
'        s_po[i].c = c; s_po[i].f = f; s_po[i].sc = 1; s_po[i].bias = bias; s_po[i].slope = slope;',
'        s_po[i].rs = rs; s_po[i].n = 0; s_po[i].next = 1;',
'        s_po_n++;',
'    }',
'    if (++s_po[i].n == s_po[i].next) {',
'        fprintf(stderr, LOGP "poly offset factor %g bias %g (guest) -> SlopeScaled %g DepthBias %d (%llu draws)\\n",',
'                (double)v->poly_factor, (double)v->poly_bias, (double)slope, (int)bias, s_po[i].n);',
'        s_po[i].next *= 10;',
'    }',
'    return s_po[i].rs;',
'}',
'',
''])
s = s.replace(a1, helper + a1, 1)

# 2. use it where the per-draw state is chosen
a2 = '    return s_rs_sc[c][f];' + nl + '}'
once(a2, 'rasterizer_state tail')
s = s.replace(a2, '    return poly_offset_state(s_rs_sc[c][f], c, f);   /* ' + MARK + ' */' + nl + '}', 1)

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
