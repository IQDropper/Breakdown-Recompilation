"""patch_d3d11_prof.py - idempotent. D3D11 backend: per-frame CPU time of the draw path, for S8 profiling.

With RECOMP_FLIP_LOG set, two lines follow each [D3D11] flip line:
  [D3D11-PROF] flip N tex T vb V drawcall D ms                         (stage 1, [d3d11-prof])
    tex      = tex_bind() for every bound stage (hash check once per flip, decode, upload)
    vb       = vertex + index streaming (vb_begin .. the index buffer Unmap)
    drawcall = the ID3D11DeviceContext_DrawIndexed submissions of normal batches
  [D3D11-PROF2] flip N surf S vsh V idx I streams B consts C ps P ms    (stage 2, [d3d11-prof2])
    d3d11_on_draw split at its phase boundaries, each phase's time summed over the frame:
    surf    = surface()            (pick or switch the render target)
    vsh     = vertex_shader()      (program lookup / compile)
    idx     = index range scan, convert_indices, rasterizer_state
    streams = vertex/index streaming and its setup (vb_begin .. index Unmap)
    consts  = constants, scissor, depth/stencil (.. the OMSetRenderTargets of the draw)
    ps      = everything after that up to the draw call (pixel shader, textures, input layout, blend)
Without RECOMP_FLIP_LOG nothing is timed (one cached flag test per region).
Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[d3d11-prof]'
MARK2 = '[d3d11-prof2]'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if MARK in s and MARK2 in s:
    print('already patched'); raise SystemExit


def once(a, what):
    n = s.count(a)
    if n != 1:
        print('REFUSED: anchor %r found %d times (want 1)' % (what, n)); raise SystemExit(1)


def sub(a, b, what):
    global s
    once(a, what)
    s = s.replace(a, b)


DRAW_TIMED = ('            uint64_t p0 = prof_now();' + nl
              + '            ID3D11DeviceContext_DrawIndexed(s_ctx, nidx, 0, 0);')

if MARK not in s:
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
        DRAW_TIMED,
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

if MARK2 not in s:
    # 6. phase buckets: a running mark, each boundary adds the time since the last one to its phase
    a6 = 'static uint64_t s_prof_tex, s_prof_vb, s_prof_draw, s_prof_vb_t0;'
    sub(a6, a6 + nl + nl.join([
        '/* ' + MARK2 + ' d3d11_on_draw phases: surf vsh idx streams consts ps */',
        'static uint64_t s_prof_ph[6], s_prof_mark;',
        '#define PROF_BEGIN() do { if (prof_on()) s_prof_mark = prof_now(); } while (0)',
        '#define PROF_MARK(i) do { if (prof_on()) { uint64_t n_ = prof_now(); s_prof_ph[i] += n_ - s_prof_mark; s_prof_mark = n_; } } while (0)']),
        'stage 1 counters')

    a7 = '    rt = surface();' + nl + '    vs = vertex_shader(d->program);'
    sub(a7, nl.join([
        '    PROF_BEGIN();',
        '    rt = surface();',
        '    PROF_MARK(0);',
        '    vs = vertex_shader(d->program);',
        '    PROF_MARK(1);']), 'surface + vertex_shader')

    a8 = '    rs = rasterizer_state(&cull_all);'
    sub(a8, a8 + nl + '    PROF_MARK(2);', 'rasterizer_state')

    a9 = '        s_prof_vb_t0 = 0;' + nl + '    }'
    sub(a9, a9 + nl + '    PROF_MARK(3);', 'stage 1 vb stop')

    a10 = '        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &rt->rtv, dsv);'
    sub(a10, '        PROF_MARK(4);' + nl + a10, 'draw OMSetRenderTargets')

    sub(DRAW_TIMED, '            PROF_MARK(5);' + nl + DRAW_TIMED, 'stage 1 timed draw')

    a11 = '    s_prof_tex = s_prof_vb = s_prof_draw = 0;'
    sub(a11, nl.join([
        '    fprintf(stderr, "[D3D11-PROF2] flip %llu surf %.2f vsh %.2f idx %.2f streams %.2f consts %.2f ps %.2f ms\\n",',
        '            (unsigned long long)s_presents, prof_ms(s_prof_ph[0]), prof_ms(s_prof_ph[1]), prof_ms(s_prof_ph[2]),',
        '            prof_ms(s_prof_ph[3]), prof_ms(s_prof_ph[4]), prof_ms(s_prof_ph[5]));',
        '    memset(s_prof_ph, 0, sizeof s_prof_ph);',
        a11]), 'stage 1 reset')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
