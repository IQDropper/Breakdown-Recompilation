"""patch_d3d11_prof.py - idempotent. D3D11 backend: per-frame CPU time of the draw path, for S8 profiling.

With RECOMP_FLIP_LOG set, a [D3D11-PROF] line follows each [D3D11] flip line:
  [D3D11-PROF] flip N tex T vb V drawcall D ms
    tex      = tex_bind() for every bound stage (hash check once per flip, decode, upload)
    vb       = vertex + index streaming (vb_begin .. the index buffer Unmap)
    drawcall = the ID3D11DeviceContext_DrawIndexed submissions of normal batches
Without RECOMP_FLIP_LOG nothing is timed (one cached flag test per region).
Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[d3d11-prof]'
s = io.open(P, encoding='utf-8', newline='').read()
if MARK in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'


def once(a, what):
    n = s.count(a)
    if n != 1:
        print('REFUSED: anchor %r found %d times (want 1)' % (what, n)); raise SystemExit(1)


def sub(a, b, what):
    global s
    once(a, what)
    s = s.replace(a, b)


# 1. counters and a QPC clock next to the texture counters
a1 = 'static uint64_t s_tex_uploads, s_tex_rehash, s_tex_changed;'
sub(a1, a1 + nl + nl.join([
    '/* ' + MARK + ' per-frame CPU time of the draw path (RECOMP_FLIP_LOG only). */',
    'static int s_prof_on = -1;',
    'static uint64_t s_prof_tex, s_prof_vb, s_prof_draw, s_prof_vb_t0;',
    'static int prof_on(void)',
    '{',
    '    if (s_prof_on < 0)',
    '        s_prof_on = recomp_env(RENV_FLIP_LOG) != NULL;',
    '    return s_prof_on;',
    '}',
    'static uint64_t prof_now(void)',
    '{',
    '    LARGE_INTEGER q;',
    '    QueryPerformanceCounter(&q);',
    '    return (uint64_t)q.QuadPart;',
    '}',
    'static double prof_ms(uint64_t ticks)',
    '{',
    '    static double per_ms;',
    '    if (!per_ms) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); per_ms = (double)f.QuadPart / 1000.0; }',
    '    return (double)ticks / per_ms;',
    '}']), 'texture counters')

# 2. texture binding
a2 = '            srv[n] = tex_bind(t);'
sub(a2, nl.join([
    '            if (prof_on()) {',
    '                uint64_t p0 = prof_now();',
    '                srv[n] = tex_bind(t);',
    '                s_prof_tex += prof_now() - p0;',
    '            } else',
    '                srv[n] = tex_bind(t);']), 'tex_bind call')

# 3. vertex + index streaming: start at vb_begin, stop after the index buffer Unmap
a3 = '        vb_begin(bound <= 0xFFFFFFFFu ? (UINT)bound : 0);'
sub(a3, nl.join([
    '        if (prof_on())',
    '            s_prof_vb_t0 = prof_now();',
    a3]), 'vb_begin call')
a3b = '    memcpy(p, idx, nidx * 4u);' + nl + '    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_ib, 0);'
sub(a3b, nl.join([
    a3b,
    '    if (prof_on() && s_prof_vb_t0) {',
    '        s_prof_vb += prof_now() - s_prof_vb_t0;',
    '        s_prof_vb_t0 = 0;',
    '    }']), 'index buffer unmap')

# 4. the normal batch's draw call (the else branch of the px trace)
a4 = ('        } else {' + nl
      + '            ID3D11DeviceContext_DrawIndexed(s_ctx, nidx, 0, 0);' + nl
      + '        }' + nl
      + '    }' + nl
      + '    rt->last_draw = ++s_seq;')
sub(a4, nl.join([
    '        } else if (prof_on()) {',
    '            uint64_t p0 = prof_now();',
    '            ID3D11DeviceContext_DrawIndexed(s_ctx, nidx, 0, 0);',
    '            s_prof_draw += prof_now() - p0;',
    '        } else {',
    '            ID3D11DeviceContext_DrawIndexed(s_ctx, nidx, 0, 0);',
    '        }',
    '    }',
    '    rt->last_draw = ++s_seq;']), 'DrawIndexed else branch')

# 5. print after the [D3D11] flip line, then reset
a5 = '    p_up = s_tex_uploads; p_chg = s_tex_changed; p_re = s_tex_rehash;'
sub(a5, nl.join([
    '    fprintf(stderr, "[D3D11-PROF] flip %llu tex %.2f vb %.2f drawcall %.2f ms\\n",',
    '            (unsigned long long)s_presents, prof_ms(s_prof_tex), prof_ms(s_prof_vb),',
    '            prof_ms(s_prof_draw));',
    '    s_prof_tex = s_prof_vb = s_prof_draw = 0;',
    a5]), 'flip line counter reset')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
