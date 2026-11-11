"""patch_pb_pipeline.py - idempotent. Opt-in (RECOMP_PB_PIPELINE=1): ack D3D's KickOff flush at once.

D3D's KickOff sets PFB 0x100410 bit 16 and spins until it clears. The toolkit's ack loop clears it only after the
walker has executed the previous segment, so the title stays within one segment of the walker and the two run in
series (S8: the game waits ~15 ms a frame in busy rooms). It does that because a title further ahead reaches
D3D_BlockOnTime's NOP-with-notify and waits on a GPU interrupt that nothing raises.

Breakdown's src/recomp_manual.c overrides D3D_BlockOnTime (0x1C0B10) so that, with RECOMP_PB_PIPELINE=1, it polls
the GPU time instead of waiting on that interrupt. This patch is the other half, with the same variable:
  stage 1 [pb-pipeline]: the nv2a-kick thread (started for it, as for RECOMP_FLIP_LOG) clears the flush bit as soon
    as it sees it, so the title can run ahead of the walker the way it runs ahead of the GPU on the console.
  stage 2 [pb-pipeline-tick]: the kick thread also refreshes PGRAPH 0x400B10 from the completed fence on every pass.
    D3D's wait-for-idle (0x1C07E0) spins on that register; the ack loop refreshes it only after walking new work, so
    a title that ran ahead and then waited with nothing new to submit never saw it move (it hung at boot).
Unset or 0: unchanged. Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\kernel\xbox_memory_layout.c'
MARK = '[pb-pipeline]'
MARK2 = '[pb-pipeline-tick]'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if MARK in s and MARK2 in s:
    print('already patched'); raise SystemExit


def sub(a, b, what):
    global s
    n = s.count(a)
    if n != 1:
        print('REFUSED: anchor %r found %d times (want 1)' % (what, n)); raise SystemExit(1)
    s = s.replace(a, b)


PIPE_IF = '            if (g_kick_pipeline) {   /* ' + MARK + ' the title may run ahead of the walker */'

if MARK not in s:
    a1 = 'static int g_kick_fast;                      /* RECOMP_FAST_KICK */'
    sub(a1, a1 + nl + 'static int g_kick_pipeline;                  /* ' + MARK + ' RECOMP_PB_PIPELINE=1: ack at once */',
        'g_kick_fast declaration')

    a2 = ('            __atomic_compare_exchange_n(&g_kick_seen_ns, &zero, now, 0,' + nl
          + '                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);' + nl
          + '            last_kick = now;')
    sub(a2, a2 + nl + nl.join([
        PIPE_IF,
        '                *r &= ~NV2A_KICK_BIT;',
        '                kick_cleared(now);',
        '            }']), 'kick thread bit seen')

    a3 = ('    if (recomp_env(RENV_FLIP_LOG))' + nl
          + '        g_nv2a_kick_thread = CreateThread(NULL, 0, nv2a_kick_thread, regs, 0, NULL);')
    sub(a3, nl.join([
        '    {   /* ' + MARK + ' */',
        '        const char *pp = getenv("RECOMP_PB_PIPELINE");',
        '        g_kick_pipeline = pp && *pp == \'1\';',
        '        if (g_kick_pipeline)',
        '            fprintf(stderr, "  NV2A KickOff flush: acked at once (RECOMP_PB_PIPELINE=1)\\n");',
        '    }',
        '    if (recomp_env(RENV_FLIP_LOG) || g_kick_pipeline)',
        '        g_nv2a_kick_thread = CreateThread(NULL, 0, nv2a_kick_thread, regs, 0, NULL);']),
        'kick thread start')

if MARK2 not in s:
    a7 = '        kick_nap_us(now - last_kick < 200000000ull ? 50 : 1000);'
    sub(a7, nl.join([
        '        if (g_kick_pipeline)   /* ' + MARK2 + ' keep 0x400B10 in step while the title waits */',
        '            pgraph_fence_tick((volatile uint32_t *)param);',
        a7]), 'kick thread nap')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
