"""patch_fb_window.py - toolkit fix (XBOXRECOMP_NOTES.md item 16): the Windows framebuffer window.
Found by Foster's first native play test (2026-11-03):
 1. Closing the window only stopped its thread; the game kept running and the next display-mode change opened
    a NEW window ("it keeps coming back"; only closing the console ended it). -> WM_CLOSE now exits the process
    (exit(0): atexit handlers run, so rumble motors are stopped by the input layer).
 2. The window was exactly 640x480 and drew at 1:1, tiny on a modern screen. -> it opens at RECOMP_WINDOW_SCALE
    (default 2, the same default the SDL host uses), is resizable, and every frame is stretched to the client
    area with HALFTONE filtering.
Idempotent; refuses if the code moved."""
import pathlib, re
P = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\external\xboxrecomp\src\video\fb_present.c')
s = P.read_text(encoding='utf-8')
MARK = '/* fbwin: see patch_fb_window.py */'
if MARK in s:
    print('already patched'); raise SystemExit
close_re = re.compile(r'(    case WM_CLOSE:\n)(    case WM_DESTROY:\n\s+InterlockedExchange\(&s_fb_running, 0\);\n\s+return 0;)')
blit_old = 'StretchDIBits(hdc, 0, 0, (int)s_fb_width, (int)s_fb_height,'
size_old = '    r.left = 0; r.top = 0; r.right = (LONG)s_fb_width; r.bottom = (LONG)s_fb_height;\n'
dc_old = '    hdc = GetDC(hwnd);\n'
wndproc = 'static LRESULT CALLBACK fb_wndproc('
assert close_re.search(s) and s.count(blit_old) == 2 and s.count(size_old) == 1 and s.count(dc_old) == 1 and s.count(wndproc) == 1, \
    'toolkit changed - patch by hand'
s = s.replace(wndproc, MARK + '\nstatic RECT s_cr;   /* client area: frames are stretched to it */\n\n' + wndproc, 1)
s = close_re.sub(lambda m: m.group(1) + '        exit(0);   /* closing the game window quits the game (it used to reopen) */\n' + m.group(2), s, 1)
s = s.replace(blit_old, 'GetClientRect(hwnd, &s_cr);\n            StretchDIBits(hdc, 0, 0, (int)s_cr.right, (int)s_cr.bottom,')
s = s.replace(size_old, '''    {
        const char *sc = recomp_env(RENV_WINDOW_SCALE);
        int k = sc ? atoi(sc) : 2;
        if (k < 1 || k > 6) k = 2;
        r.left = 0; r.top = 0; r.right = (LONG)(s_fb_width * k); r.bottom = (LONG)(s_fb_height * k);
    }
''')
s = s.replace(dc_old, dc_old + '    SetStretchBltMode(hdc, HALFTONE); SetBrushOrgEx(hdc, 0, 0, NULL);\n')
P.write_text(s, encoding='utf-8')
print('patched fb_present.c: close quits, 2x window, stretched blit')
