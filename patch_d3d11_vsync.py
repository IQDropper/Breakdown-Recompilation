"""patch_d3d11_vsync.py - idempotent. D3D11 backend: VSync can be switched while the game runs.

The backend reads RECOMP_PRESENT_VSYNC once at init into s_sync_interval (Present's sync interval: 0 = don't wait for
the display, 1 = wait). This adds nv2a_d3d11_set_vsync(on) / nv2a_d3d11_vsync() so the PC settings page can change it
live; the next Present uses the new value. Anchors must each occur once; refuses otherwise."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
MARK = '[d3d11-vsync]'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if MARK in s:
    print('already patched'); raise SystemExit

a = 'static UINT                    s_sync_interval;'
n = s.count(a)
if n != 1:
    print('REFUSED: anchor %r found %d times (want 1)' % (a, n)); raise SystemExit(1)
s = s.replace(a, 'static volatile UINT           s_sync_interval;' + nl + nl.join([
    '/* ' + MARK + ' switch VSync while running (PC settings page); read by the next Present. */',
    'void nv2a_d3d11_set_vsync(int on)',
    '{',
    '    s_sync_interval = on ? 1u : 0u;',
    '}',
    'int nv2a_d3d11_vsync(void)',
    '{',
    '    return s_sync_interval != 0;',
    '}',
]))
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
