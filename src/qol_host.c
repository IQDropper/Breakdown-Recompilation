/*
 * qol_host.c - S5.3 quality of life, the Windows side. The math and the PNG writer are in src/qol.c (tested).
 *
 *   FPS overlay (RECOMP_FPS_OVERLAY=1): qol_frame() runs once per game frame (src/recomp_manual.c, at
 *     Render_SubmitFrame's DebugPrintf from 0x18EA4E) and stamps the time. A click-through popup in the top left
 *     corner of the 4:3 picture shows the average FPS and frame time over the last second, and the slowest frame.
 *     F5 shows/hides it.
 *   F12 screenshot (on unless RECOMP_SCREENSHOT=0): saves the 4:3 picture as a PNG to
 *     %LOCALAPPDATA%\xboxrecomp\screenshots\breakdown_<date>_<time>.png. Capture: PrintWindow with
 *     PW_RENDERFULLCONTENT (reads the D3D11 window's own picture); if that comes back black, a copy of the
 *     screen under the window. The overlay then shows "screenshot saved" for 2 seconds.
 *   Keys are polled (GetAsyncKeyState) only while the game window is in front, like the speedrun timer's F11.
 *   Same pattern as src/speedrun_host.c and src/debug_text_host.c.
 */
#include "qol_host.h"
#include "qol.h"
#include "pc_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x1
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x2
#endif

#define GAME_CLASS "RecompNv2aD3D11"
#define QOL_CLASS  "BreakdownQol"
#define KEY_COLOR  RGB(255, 0, 255)
#define BOX_COLOR  RGB(12, 12, 16)
#define NOTE_MS    2000u

static CRITICAL_SECTION s_lock;
static volatile int s_ready;
static int s_fps_on = -1, s_shots_on = -1, s_skip_on = -1;
static struct qol_fps s_fps;
static volatile int s_visible = 1;
static char s_note[96];
static uint64_t s_note_until;
static HWND s_win;
static HFONT s_font;
static int s_font_h;
static LARGE_INTEGER s_freq;

static int env_is(const char *name, char want)
{
    const char *e = getenv(name);
    return e && *e == want;
}

int qol_fps_overlay_on(void)
{
    if (s_fps_on < 0)
        s_fps_on = env_is("RECOMP_FPS_OVERLAY", '1');
    return s_fps_on;
}

int qol_screenshots_on(void)
{
    if (s_shots_on < 0)
        s_shots_on = !env_is("RECOMP_SCREENSHOT", '0');
    return s_shots_on;
}

int qol_skip_intro_on(void)
{
    if (s_skip_on < 0)
        s_skip_on = env_is("RECOMP_SKIP_INTRO", '1');
    return s_skip_on;
}

static uint64_t now_us(void)
{
    LARGE_INTEGER c;
    uint64_t f = (uint64_t)s_freq.QuadPart ? (uint64_t)s_freq.QuadPart : 1;
    QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart / f * 1000000u + (uint64_t)c.QuadPart % f * 1000000u / f;
}

void qol_note(const char *text)
{
    if (!s_ready)
        return;
    EnterCriticalSection(&s_lock);
    snprintf(s_note, sizeof s_note, "%s", text);
    s_note_until = now_us() + NOTE_MS * 1000u;
    LeaveCriticalSection(&s_lock);
}

/* ---- screenshot ---- */

static int shot_path(char *out, size_t n)
{
    const char *la = getenv("LOCALAPPDATA");
    char dir[MAX_PATH], name[64];
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    int seq;
    snprintf(dir, sizeof dir, "%s\\xboxrecomp", la && *la ? la : ".");
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\xboxrecomp\\screenshots", la && *la ? la : ".");
    CreateDirectoryA(dir, NULL);
    for (seq = 1; seq < 100; seq++) {
        if (lt)
            qol_shot_name(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, seq,
                          name, sizeof name);
        else
            snprintf(name, sizeof name, "breakdown_%d.png", seq);
        snprintf(out, n, "%s\\%s", dir, name);
        if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
}

/* 1 when a grid of samples inside the rect is all black (the capture got nothing). */
static int looks_black(const uint8_t *bits, int stride, const struct pc_rect *r)
{
    int i, j;
    for (j = 0; j < 8; j++)
        for (i = 0; i < 8; i++) {
            int x = r->x + r->w * (2 * i + 1) / 16, y = r->y + r->h * (2 * j + 1) / 16;
            const uint8_t *p = bits + (size_t)y * stride + (size_t)x * 4;
            if (p[0] | p[1] | p[2])
                return 0;
        }
    return 1;
}

static void screenshot(HWND game)
{
    RECT cr;
    struct pc_rect fr;
    BITMAPINFO bi;
    HDC wdc, mem;
    HBITMAP dib, old;
    void *bits = NULL;
    uint8_t *png;
    size_t cap, n = 0;
    char path[MAX_PATH];
    int cw, ch, stride, how = 1;
    FILE *f;

    if (!GetClientRect(game, &cr) || cr.right < 1 || cr.bottom < 1)
        return;
    cw = cr.right;
    ch = cr.bottom;
    pc_options_frame_rect((unsigned)cw, (unsigned)ch, &fr);
    if (fr.w < 1 || fr.h < 1 || fr.x < 0 || fr.y < 0 || fr.x + fr.w > cw || fr.y + fr.h > ch) {
        fr.x = fr.y = 0;
        fr.w = cw;
        fr.h = ch;
    }
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;   /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    wdc = GetDC(game);
    mem = CreateCompatibleDC(wdc);
    dib = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!mem || !dib || !bits) {
        fprintf(stderr, "[SHOT] could not make a %dx%d bitmap\n", cw, ch);
        if (dib)
            DeleteObject(dib);
        if (mem)
            DeleteDC(mem);
        ReleaseDC(game, wdc);
        return;
    }
    old = (HBITMAP)SelectObject(mem, dib);
    stride = cw * 4;
    if (!PrintWindow(game, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT) || (GdiFlush(), looks_black(bits, stride, &fr))) {
        /* the screen under the window; hide our own box first so it is not in the picture */
        HDC sdc = GetDC(NULL);
        POINT o = { 0, 0 };
        int was = s_win && IsWindowVisible(s_win);
        if (was) {
            ShowWindow(s_win, SW_HIDE);
            Sleep(50);
        }
        ClientToScreen(game, &o);
        BitBlt(mem, 0, 0, cw, ch, sdc, o.x, o.y, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        ReleaseDC(NULL, sdc);
        if (was)
            ShowWindow(s_win, SW_SHOWNOACTIVATE);
        how = 2;
    }

    cap = qol_png_size(fr.w, fr.h);
    png = cap ? malloc(cap) : NULL;
    if (png)
        n = qol_png_encode((const uint8_t *)bits + (size_t)fr.y * stride + (size_t)fr.x * 4, fr.w, fr.h, stride,
                           png, cap);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(game, wdc);

    if (!n) {
        fprintf(stderr, "[SHOT] could not encode %dx%d\n", fr.w, fr.h);
        free(png);
        qol_note("screenshot failed");
        return;
    }
    if (!shot_path(path, sizeof path) || !(f = fopen(path, "wb"))) {
        fprintf(stderr, "[SHOT] could not open a file in %%LOCALAPPDATA%%\\xboxrecomp\\screenshots\n");
        free(png);
        qol_note("screenshot failed");
        return;
    }
    n = fwrite(png, 1, n, f) == n;
    fclose(f);
    free(png);
    fprintf(stderr, "[SHOT] %s %dx%d (%s)\n", n ? "saved" : "write failed:", fr.w, fr.h,
            how == 1 ? "window" : "screen");
    fprintf(stderr, "[SHOT] %s\n", path);
    fflush(stderr);
    qol_note(n ? "screenshot saved" : "screenshot failed");
}

/* ---- game frames ---- */

static void start(void);

void qol_frame(void)
{
    static int started;
    if (!started) {
        started = 1;
        start();
    }
    if (s_ready && s_fps_on) {
        uint64_t t = now_us();
        EnterCriticalSection(&s_lock);
        qol_fps_frame(&s_fps, t);
        LeaveCriticalSection(&s_lock);
    }
}

/* ---- overlay ---- */

/* What to show now: the FPS line and/or a note. Returns the line count (0 = hide the box). */
static int lines_now(char fps[96], COLORREF *fps_col, char note[96])
{
    struct qol_fps_stats st;
    int have, n = 0;
    uint64_t t = now_us();
    fps[0] = note[0] = 0;
    EnterCriticalSection(&s_lock);
    have = qol_fps_stats(&s_fps, t, &st);
    if (t < s_note_until)
        snprintf(note, 96, "%s", s_note);
    LeaveCriticalSection(&s_lock);
    if (s_fps_on && s_visible) {
        qol_fps_format(&st, have, fps, 96);
        *fps_col = !have ? RGB(180, 180, 180) : st.fps >= 57.0 ? RGB(120, 230, 120)
                 : st.fps >= 28.0 ? RGB(235, 220, 120) : RGB(240, 120, 110);
        n++;
    }
    if (note[0])
        n++;
    return n;
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc, box;
    HDC dc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, old_bmp;
    HBRUSH br;
    char fps[96], note[96];
    COLORREF col = RGB(235, 235, 235);
    int w, h, lh, pad, lines;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    lines = lines_now(fps, &col, note);
    if (w < 1 || h < 1 || lines < 1) {
        EndPaint(hwnd, &ps);
        return;
    }
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);
    br = CreateSolidBrush(BOX_COLOR);
    FillRect(mem, &rc, br);
    DeleteObject(br);
    lh = h / lines;
    pad = lh / 5;
    if (!s_font || s_font_h != lh) {
        if (s_font)
            DeleteObject(s_font);
        s_font = CreateFontA(-(lh * 3 / 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        s_font_h = lh;
    }
    SelectObject(mem, s_font);
    SetBkMode(mem, TRANSPARENT);
    box.left = pad;
    box.right = w - pad;
    box.top = 0;
    box.bottom = lh;
    if (fps[0]) {
        SetTextColor(mem, col);
        DrawTextA(mem, fps, -1, &box, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        box.top += lh;
        box.bottom += lh;
    }
    if (note[0]) {
        SetTextColor(mem, RGB(235, 235, 235));
        DrawTextA(mem, note, -1, &box, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK qol_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT) {
        paint(hwnd);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/* Top left corner of the 4:3 picture (the speedrun timer is top right). */
static void place(HWND game, int repaint)
{
    RECT client;
    struct pc_rect fr;
    POINT o;
    char a[96], b[96];
    COLORREF c;
    int lines = lines_now(a, &c, b), bw, bh, margin, show;
    if (!GetClientRect(game, &client))
        return;
    pc_options_frame_rect((unsigned)client.right, (unsigned)client.bottom, &fr);
    if (fr.w < 1 || fr.h < 1)
        return;
    show = lines > 0 && !IsIconic(game);
    bw = fr.w * 36 / 100;
    bh = fr.h * 5 / 100 * (lines > 0 ? lines : 1);
    margin = fr.h / 40;
    o.x = fr.x + margin;
    o.y = fr.y + margin;
    ClientToScreen(game, &o);
    SetWindowPos(s_win, NULL, o.x, o.y, bw, bh,
                 SWP_NOZORDER | SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (show && repaint)
        InvalidateRect(s_win, NULL, FALSE);
}

static int game_in_front(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

/* F5: FPS overlay on/off (when RECOMP_FPS_OVERLAY=1). F12: screenshot. */
static void keys(HWND game)
{
    static int s_was5, s_was12;
    int d5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    int d12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    int front = (d5 && !s_was5) || (d12 && !s_was12) ? game_in_front() : 0;
    if (d5 && !s_was5 && front && s_fps_on)
        s_visible = !s_visible;
    if (d12 && !s_was12 && front && s_shots_on && IsWindow(game))
        screenshot(game);
    s_was5 = d5;
    s_was12 = d12;
}

static DWORD WINAPI qol_thread(LPVOID unused)
{
    WNDCLASSA wc;
    HWND game = NULL;
    MSG m;
    unsigned tick = 0;
    (void)unused;
    while (!game) {
        game = FindWindowA(GAME_CLASS, NULL);
        if (!game)
            Sleep(250);
    }
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;   /* the box grows when a note is added */
    wc.lpfnWndProc = qol_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = QOL_CLASS;
    RegisterClassA(&wc);
    s_win = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, QOL_CLASS,
                            "fps overlay", WS_POPUP, 0, 0, 10, 10, game, NULL, wc.hInstance, NULL);
    if (!s_win) {
        fprintf(stderr, "[QOL] could not create the overlay (%lu)\n", GetLastError());
        return 0;
    }
    SetLayeredWindowAttributes(s_win, KEY_COLOR, 200, LWA_COLORKEY | LWA_ALPHA);
    SetTimer(s_win, 1, 33, NULL);
    fprintf(stderr, "[QOL] ready:%s%s\n", s_fps_on ? " FPS overlay (F5 shows/hides)" : "",
            s_shots_on ? " F12 screenshot to %LOCALAPPDATA%\\xboxrecomp\\screenshots" : "");
    fflush(stderr);
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            if (!IsWindow(game)) {
                HWND g = FindWindowA(GAME_CLASS, NULL);
                if (g)
                    game = g;
            }
            keys(game);
            if (IsWindow(game))
                place(game, (++tick % 8) == 0);   /* the numbers change 4 times a second */
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

static void start(void)
{
    HANDLE t;
    qol_fps_overlay_on();
    qol_screenshots_on();
    if (!s_fps_on && !s_shots_on)
        return;
    QueryPerformanceFrequency(&s_freq);
    InitializeCriticalSection(&s_lock);
    qol_fps_init(&s_fps);
    s_ready = 1;
    t = CreateThread(NULL, 0, qol_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
