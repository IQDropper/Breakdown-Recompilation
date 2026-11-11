/*
 * speedrun_host.c - the speedrun timer overlay. Off unless RECOMP_SPEEDRUN=1.
 *
 * The timing rules live in src/speedrun.c (pure, unit tested). This file feeds them:
 *   - file opens: the toolkit's g_xbox_path_hook (kernel_path.c) hands over every guest path the game opens;
 *     area paks ("area\<name>\<name>_A.pak") start a load and add a split.
 *   - pause: speedrun_frame() runs once per frame (and starts all of this on the first one) (src/recomp_manual.c, at Render_SubmitFrame's DebugPrintf)
 *     with the game's pause flag 0x29F6E0 (0 = playing, 1 = paused).
 *   - keys (game window in front): F11 shows/hides the timer, Ctrl+F11 resets it (the run restarts on the
 *     next area load).
 * The timer is drawn in a click-through popup in the top right corner of the 4:3 picture (same idea as
 * src/debug_text_host.c). Splits are added to %LOCALAPPDATA%\xboxrecomp\speedrun\splits.csv.
 */
#include "speedrun_host.h"
#include "speedrun.h"
#include "pc_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

#define GAME_CLASS "RecompNv2aD3D11"
#define SR_CLASS   "BreakdownSpeedrun"
#define KEY_COLOR  RGB(255, 0, 255)
#define BOX_COLOR  RGB(12, 12, 16)
#define LINES 3

/* The toolkit's optional path hook (external/xboxrecomp/src/kernel/kernel.h). */
extern void (*g_xbox_path_hook)(const char *xbox_path);
static void (*s_prev_hook)(const char *xbox_path);

static CRITICAL_SECTION s_lock;
static volatile int s_ready;
static int s_on = -1;
static struct speedrun s_run;
static char s_run_id[32];
static char s_csv[MAX_PATH];
static volatile int s_visible = 1;
static HWND s_win;
static HFONT s_font_big, s_font_small;
static int s_font_h;
static LARGE_INTEGER s_freq;

int speedrun_on(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_SPEEDRUN");
        s_on = e && *e == '1';
    }
    return s_on;
}

static uint64_t now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / (s_freq.QuadPart / 1000 ? s_freq.QuadPart / 1000 : 1));
}

/* ---- splits.csv ---- */

static void csv_path(void)
{
    const char *la = getenv("LOCALAPPDATA");
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\xboxrecomp", la && *la ? la : ".");
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\xboxrecomp\\speedrun", la && *la ? la : ".");
    CreateDirectoryA(dir, NULL);
    snprintf(s_csv, sizeof s_csv, "%s\\splits.csv", dir);
}

/* Call with s_lock held. */
static void csv_write(const char *row)
{
    FILE *f;
    int fresh;
    if (!row[0])
        return;
    f = fopen(s_csv, "rb");
    fresh = f == NULL;
    if (f)
        fclose(f);
    f = fopen(s_csv, "ab");
    if (!f) {
        fprintf(stderr, "[SPEEDRUN] could not write %s\n", s_csv);
        return;
    }
    if (fresh)
        fprintf(f, "%s\r\n", SPEEDRUN_CSV_HEADER);
    fprintf(f, "%s\r\n", row);
    fclose(f);
}

static void new_run_id(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt)
        strftime(s_run_id, sizeof s_run_id, "%Y-%m-%d_%H-%M-%S", lt);
    else
        snprintf(s_run_id, sizeof s_run_id, "run");
}

/* Call with s_lock held: write the end row (if a run is going) and go back to READY. */
static void end_run(const char *why)
{
    char row[200];
    speedrun_csv_end(&s_run, now_ms(), s_run_id, row, sizeof row);
    if (row[0]) {
        csv_write(row);
        fprintf(stderr, "[SPEEDRUN] %s: %s\n", why, row);
        fflush(stderr);
    }
    speedrun_reset(&s_run);
}

/* ---- game events ---- */

static void on_path(const char *xbox_path)
{
    if (s_ready && xbox_path) {
        enum speedrun_event ev;
        EnterCriticalSection(&s_lock);
        ev = speedrun_on_path(&s_run, xbox_path, now_ms());
        if (ev != SPEEDRUN_EV_NONE) {
            char row[200];
            if (ev == SPEEDRUN_EV_START)
                new_run_id();
            speedrun_csv_row(&s_run, s_run.nsplits - 1, s_run_id, row, sizeof row);
            csv_write(row);
            fprintf(stderr, "[SPEEDRUN] %s %s\n", ev == SPEEDRUN_EV_START ? "start" : "split", row);
            fflush(stderr);
        }
        LeaveCriticalSection(&s_lock);
    }
    if (s_prev_hook)
        s_prev_hook(xbox_path);
}

void speedrun_frame(int paused)
{
    if (!s_ready) {
        speedrun_host_start();   /* the first frame comes long before the first area load */
        if (!s_ready)
            return;
    }
    paused = paused != 0;
    if (paused == s_run.paused)
        return;   /* a plain read; the change itself is made under the lock */
    EnterCriticalSection(&s_lock);
    speedrun_set_paused(&s_run, paused, now_ms());
    LeaveCriticalSection(&s_lock);
}

static void at_exit(void)
{
    if (!s_ready)
        return;
    EnterCriticalSection(&s_lock);
    end_run("exit");
    LeaveCriticalSection(&s_lock);
}

/* ---- overlay ---- */

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc, box;
    HDC dc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, old_bmp;
    HBRUSH br;
    char igt[32], line[2][96];
    int w, h, lh, pad;
    uint64_t t, ms;
    struct speedrun copy;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    if (w < 1 || h < 1) {
        EndPaint(hwnd, &ps);
        return;
    }
    EnterCriticalSection(&s_lock);
    t = now_ms();
    ms = speedrun_igt(&s_run, t);
    copy = s_run;
    LeaveCriticalSection(&s_lock);

    speedrun_format(ms, igt, sizeof igt);
    if (copy.state == SPEEDRUN_READY) {
        snprintf(line[0], sizeof line[0], "ready");
        snprintf(line[1], sizeof line[1], "starts on the next area load");
    } else {
        char rta[32];
        const struct speedrun_split *last = &copy.splits[copy.nsplits - 1];
        speedrun_format(speedrun_rta(&copy, t), rta, sizeof rta);
        snprintf(line[0], sizeof line[0], "%s  #%d %s", speedrun_loading(&copy, t) ? "LOAD" : copy.paused ? "PAUSE" : "",
                 copy.nsplits - 1, last->area);
        snprintf(line[1], sizeof line[1], "real %s", rta);
    }

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);
    br = CreateSolidBrush(BOX_COLOR);
    FillRect(mem, &rc, br);
    DeleteObject(br);

    lh = h / (LINES + 1);
    pad = lh / 4;
    if (!s_font_big || s_font_h != lh) {
        if (s_font_big)
            DeleteObject(s_font_big);
        if (s_font_small)
            DeleteObject(s_font_small);
        s_font_big = CreateFontA(-(lh * 3 / 2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 FIXED_PITCH | FF_MODERN, "Consolas");
        s_font_small = CreateFontA(-(lh * 3 / 4), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                   FIXED_PITCH | FF_MODERN, "Consolas");
        s_font_h = lh;
    }
    SetBkMode(mem, TRANSPARENT);
    box.left = pad;
    box.right = w - pad;
    box.top = pad / 2;
    box.bottom = box.top + lh * 3 / 2 + pad;
    SelectObject(mem, s_font_big);
    /* green while counting, grey while it is stopped (ready, loading, paused) */
    SetTextColor(mem, copy.state == SPEEDRUN_RUNNING && !copy.paused && !speedrun_loading(&copy, t)
                          ? RGB(120, 230, 120) : RGB(180, 180, 180));
    DrawTextA(mem, igt, -1, &box, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(mem, s_font_small);
    SetTextColor(mem, RGB(235, 235, 235));
    box.top = box.bottom;
    box.bottom = box.top + lh;
    DrawTextA(mem, line[0], -1, &box, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    box.top = box.bottom;
    box.bottom = box.top + lh;
    DrawTextA(mem, line[1], -1, &box, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK sr_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
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

/* Top right corner of the 4:3 picture: about a quarter of its width, a seventh of its height. */
static void place(HWND game)
{
    RECT client;
    struct pc_rect fr;
    POINT o;
    int bw, bh, margin;
    if (!GetClientRect(game, &client))
        return;
    pc_options_frame_rect((unsigned)client.right, (unsigned)client.bottom, &fr);
    if (fr.w < 1 || fr.h < 1)
        return;
    bw = fr.w * 30 / 100;
    bh = fr.h * 15 / 100;
    margin = fr.h / 40;
    o.x = fr.x + fr.w - bw - margin;
    o.y = fr.y + margin;
    ClientToScreen(game, &o);
    SetWindowPos(s_win, NULL, o.x, o.y, bw, bh,
                 SWP_NOZORDER | SWP_NOACTIVATE | (s_visible && !IsIconic(game) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (s_visible)
        InvalidateRect(s_win, NULL, FALSE);
}

static int game_in_front(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

/* F11: show/hide. Ctrl+F11: reset. Polled, so the game and the PC options page keep their own keys. */
static void keys(void)
{
    static int s_was;
    int down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (down && !s_was && game_in_front()) {
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            EnterCriticalSection(&s_lock);
            end_run("reset");
            LeaveCriticalSection(&s_lock);
            fprintf(stderr, "[SPEEDRUN] reset: the run starts on the next area load\n");
            fflush(stderr);
        } else {
            s_visible = !s_visible;
        }
    }
    s_was = down;
}

static DWORD WINAPI sr_thread(LPVOID unused)
{
    WNDCLASSA wc;
    HWND game = NULL;
    MSG m;
    (void)unused;
    while (!game) {
        game = FindWindowA(GAME_CLASS, NULL);
        if (!game)
            Sleep(250);
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = sr_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = SR_CLASS;
    RegisterClassA(&wc);
    s_win = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            SR_CLASS, "speedrun timer", WS_POPUP, 0, 0, 10, 10, game, NULL, wc.hInstance, NULL);
    if (!s_win) {
        fprintf(stderr, "[SPEEDRUN] could not create the overlay (%lu)\n", GetLastError());
        return 0;
    }
    /* mostly solid: a dark box that still lets a little of the picture through */
    SetLayeredWindowAttributes(s_win, KEY_COLOR, 215, LWA_COLORKEY | LWA_ALPHA);
    SetTimer(s_win, 1, 33, NULL);
    fprintf(stderr, "[SPEEDRUN] overlay ready: F11 shows/hides, Ctrl+F11 resets; splits go to %s\n", s_csv);
    fflush(stderr);
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            if (!IsWindow(game)) {
                HWND g = FindWindowA(GAME_CLASS, NULL);
                if (g)
                    game = g;
            }
            keys();
            if (IsWindow(game))
                place(game);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

void speedrun_host_start(void)
{
    static int started;
    HANDLE t;
    if (started || !speedrun_on())
        return;
    started = 1;
    QueryPerformanceFrequency(&s_freq);
    InitializeCriticalSection(&s_lock);
    speedrun_init(&s_run);
    csv_path();
    s_ready = 1;
    s_prev_hook = g_xbox_path_hook;
    g_xbox_path_hook = on_path;
    atexit(at_exit);
    t = CreateThread(NULL, 0, sr_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
