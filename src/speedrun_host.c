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
 * src/debug_text_host.c). Splits are added to %LOCALAPPDATA%\xboxrecomp\speedrun\splits.csv
 * (RECOMP_DATA_DIR\speedrun when set: host_paths.h).
 * Look: speedrun_theme.ini (src/speedrun_theme.c; every key in speedrun_theme.example.ini): a title line, font,
 * size, colors, box or text only, opacity, corner, which lines show, and the player's own split names. Read
 * again about once a second, so edits show while the game runs. No file: the look above, and splits show the
 * game's own area names ("7F / Main Research Center") instead of file names.
 */
#include "speedrun_host.h"
#include "speedrun.h"
#include "pc_options.h"
#include "host_paths.h"
#include "display_host.h"
#include "speedrun_theme.h"
#include "play_record.h"   /* pr_area_title: the game's own area names */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

#define GAME_CLASS "RecompNv2aD3D11"
#define SR_CLASS   "BreakdownSpeedrun"
#define KEY_COLOR  RGB(255, 0, 255)

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
    char dir[MAX_PATH];
    host_data_dir(dir, sizeof dir, "speedrun");
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

/* ---- theme (speedrun_theme.ini) ---- */

/* Only the overlay thread touches these. */
static struct sr_theme s_theme;
static unsigned s_theme_ver;          /* bumped on each (re)load: fonts, alpha and size follow */
static char s_theme_path[MAX_PATH];
static FILETIME s_theme_stamp;

/* RECOMP_SPEEDRUN_THEME=<file>; else speedrun_theme.ini in the folder the game was started from (where
 * run_native.cmd is), beside the exe, or one folder up from the exe (the repo when the exe is in build-win). */
static int theme_find(char *out, size_t n)
{
    const char *e = getenv("RECOMP_SPEEDRUN_THEME");
    char exe[MAX_PATH], *p;
    if (e && *e) {
        snprintf(out, n, "%s", e);
        return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
    }
    snprintf(out, n, "speedrun_theme.ini");
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES)
        return 1;
    if (!GetModuleFileNameA(NULL, exe, sizeof exe) || !(p = strrchr(exe, '\\')))
        return 0;
    *p = 0;
    snprintf(out, n, "%s\\speedrun_theme.ini", exe);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES)
        return 1;
    if (!(p = strrchr(exe, '\\')))
        return 0;
    *p = 0;
    snprintf(out, n, "%s\\speedrun_theme.ini", exe);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

static const char *corner_word(enum sr_corner c)
{
    return c == SR_TOP_RIGHT ? "top right" : c == SR_TOP_LEFT ? "top left"
         : c == SR_BOTTOM_RIGHT ? "bottom right" : "bottom left";
}

/* Looked at about once a second, so a player can edit the file with the game running. */
static void theme_check(void)
{
    char path[MAX_PATH], *text;
    WIN32_FILE_ATTRIBUTE_DATA a;
    FILE *f;
    long len;
    int bad;
    if (!theme_find(path, sizeof path) || !GetFileAttributesExA(path, GetFileExInfoStandard, &a)) {
        if (s_theme_path[0]) {   /* the file went away: back to the default look */
            s_theme_path[0] = 0;
            sr_theme_default(&s_theme);
            s_theme_ver++;
            fprintf(stderr, "[SPEEDRUN] theme file gone: default look\n");
            fflush(stderr);
        }
        return;
    }
    if (!strcmp(path, s_theme_path) && CompareFileTime(&a.ftLastWriteTime, &s_theme_stamp) == 0)
        return;
    f = fopen(path, "rb");
    if (!f)
        return;   /* still being written: try again next time */
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 256 * 1024 || !(text = malloc((size_t)len + 1))) {
        fclose(f);
        return;
    }
    len = (long)fread(text, 1, (size_t)len, f);
    fclose(f);
    text[len] = 0;
    sr_theme_default(&s_theme);
    bad = sr_theme_parse(&s_theme, text);
    free(text);
    snprintf(s_theme_path, sizeof s_theme_path, "%s", path);
    s_theme_stamp = a.ftLastWriteTime;
    s_theme_ver++;
    fprintf(stderr, "[SPEEDRUN] theme %s: title \"%s\", font %s %d%%, %s, %d split name(s)\n", path, s_theme.title,
            s_theme.font, s_theme.size, corner_word(s_theme.corner), s_theme.nnames);
    if (bad)
        fprintf(stderr, "[SPEEDRUN] theme: %d line(s) not understood, those keep the default "
                        "(speedrun_theme.example.ini lists every setting)\n", bad);
    fflush(stderr);
}

static COLORREF theme_rgb(uint32_t c)
{
    COLORREF r = RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
    return r == KEY_COLOR ? RGB(254, 0, 255) : r;   /* the see-through key color would punch a hole */
}

/* Height of the timer in eighths of a text line: margins 1+1, the big time 14 (1.5 lines + space), the title and
 * each small line 8. The default (no title, two small lines) is 32: the old four lines. */
static int theme_rows8(void)
{
    return 16 + 8 * ((s_theme.title[0] != 0) + s_theme.show_split + s_theme.show_real);
}

static void draw_utf8(HDC dc, const char *s, RECT *r, UINT align)
{
    WCHAR w[256];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, (int)(sizeof w / sizeof w[0]));
    if (n <= 0)   /* not UTF-8 (a file saved as ANSI): the Windows code page */
        n = MultiByteToWideChar(CP_ACP, 0, s, -1, w, (int)(sizeof w / sizeof w[0]));
    if (n > 0)
        DrawTextW(dc, w, -1, r, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static HFONT theme_font(const char *face, int px, int weight)
{
    WCHAR wf[LF_FACESIZE];
    if (MultiByteToWideChar(CP_UTF8, 0, face, -1, wf, LF_FACESIZE) <= 0)
        lstrcpyW(wf, L"Consolas");
    /* No box: plain (not smoothed) edges, or the see-through key color would show as a pink fringe. */
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, s_theme.box ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, wf);
}

/* ---- overlay ---- */

static void paint(HWND hwnd)
{
    static unsigned s_font_ver;
    PAINTSTRUCT ps;
    RECT rc, box;
    HDC dc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, old_bmp;
    HBRUSH br;
    char igt[32], split[160], real[64], name[SR_THEME_TEXT_LEN];
    int w, h, lh, pad;
    UINT align = s_theme.corner == SR_TOP_LEFT || s_theme.corner == SR_BOTTOM_LEFT ? DT_LEFT : DT_RIGHT;
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
        snprintf(split, sizeof split, "ready");
        snprintf(real, sizeof real, "starts on the next area load");
    } else {
        char rta[32];
        const struct speedrun_split *last = &copy.splits[copy.nsplits - 1];
        speedrun_format(speedrun_rta(&copy, t), rta, sizeof rta);
        /* the player's name for the area (speedrun_theme.ini [splits]), else the game's own area name */
        sr_theme_split_name(&s_theme, last->area, pr_area_title, name, sizeof name);
        {
            const char *st = speedrun_loading(&copy, t) ? "LOAD  " : copy.paused ? "PAUSE  " : "";
            snprintf(split, sizeof split, "%s#%d %s", st, copy.nsplits - 1, name);
        }
        snprintf(real, sizeof real, "real %s", rta);
    }

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);
    br = CreateSolidBrush(s_theme.box ? theme_rgb(s_theme.color_box) : KEY_COLOR);
    FillRect(mem, &rc, br);
    DeleteObject(br);

    lh = h * 8 / theme_rows8();
    pad = lh / 4;
    if (!s_font_big || s_font_h != lh || s_font_ver != s_theme_ver) {
        if (s_font_big)
            DeleteObject(s_font_big);
        if (s_font_small)
            DeleteObject(s_font_small);
        s_font_big = theme_font(s_theme.font, lh * 3 / 2, s_theme.bold ? FW_BOLD : FW_NORMAL);
        s_font_small = theme_font(s_theme.font, lh * 3 / 4, FW_NORMAL);
        s_font_h = lh;
        s_font_ver = s_theme_ver;
    }
    SetBkMode(mem, TRANSPARENT);
    box.left = pad;
    box.right = w - pad;
    box.top = lh / 8;
    if (s_theme.title[0]) {
        SelectObject(mem, s_font_small);
        SetTextColor(mem, theme_rgb(s_theme.color_title));
        box.bottom = box.top + lh;
        draw_utf8(mem, s_theme.title, &box, align);
        box.top = box.bottom;
    }
    box.bottom = box.top + lh * 3 / 2 + pad;
    SelectObject(mem, s_font_big);
    /* the running color while counting, the stopped color while ready, loading or paused */
    SetTextColor(mem, theme_rgb(copy.state == SPEEDRUN_RUNNING && !copy.paused && !speedrun_loading(&copy, t)
                                    ? s_theme.color_running : s_theme.color_stopped));
    draw_utf8(mem, igt, &box, align);
    SelectObject(mem, s_font_small);
    SetTextColor(mem, theme_rgb(s_theme.color_text));
    if (s_theme.show_split) {
        box.top = box.bottom;
        box.bottom = box.top + lh;
        draw_utf8(mem, split, &box, align);
    }
    if (s_theme.show_real) {
        box.top = box.bottom;
        box.bottom = box.top + lh;
        draw_utf8(mem, real, &box, align);
    }

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

/* A corner of the 4:3 picture (top right unless the theme says otherwise). At size 100 with the default lines:
 * about a quarter of its width, a seventh of its height. A title or fewer lines change the height. */
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
    margin = fr.h / 40;
    bw = fr.w * 30 / 100 * s_theme.size / 100;
    bh = fr.h * 15 / 100 * s_theme.size / 100 * theme_rows8() / 32;
    if (bw > fr.w - 2 * margin)
        bw = fr.w - 2 * margin;
    if (bh > fr.h - 2 * margin)
        bh = fr.h - 2 * margin;
    o.x = s_theme.corner == SR_TOP_LEFT || s_theme.corner == SR_BOTTOM_LEFT ? fr.x + margin : fr.x + fr.w - bw - margin;
    o.y = s_theme.corner == SR_TOP_LEFT || s_theme.corner == SR_TOP_RIGHT ? fr.y + margin : fr.y + fr.h - bh - margin;
    ClientToScreen(game, &o);
    SetWindowPos(s_win, NULL, o.x, o.y, bw, bh,
                 SWP_NOZORDER | SWP_NOACTIVATE | (s_visible && !IsIconic(game) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    /* Always on, so it does not hold exclusive fullscreen off: in exclusive it is not seen. Say so once. */
    if (s_visible && display_exclusive_on()) {
        static int s_said;
        if (!s_said) {
            s_said = 1;
            fprintf(stderr, "[SPEEDRUN] timer cannot show in exclusive fullscreen (use borderless to see it)\n");
            fflush(stderr);
        }
    }
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
    SetTimer(s_win, 1, 33, NULL);
    fprintf(stderr, "[SPEEDRUN] overlay ready: F11 shows/hides, Ctrl+F11 resets; splits go to %s\n", s_csv);
    fflush(stderr);
    sr_theme_default(&s_theme);
    theme_check();
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            static int s_tick;
            static unsigned s_alpha_ver = ~0u;
            if (++s_tick >= 30) {   /* about once a second: speedrun_theme.ini edited, added or removed */
                s_tick = 0;
                theme_check();
            }
            if (s_alpha_ver != s_theme_ver) {
                /* default 84%: mostly solid, a dark box that still lets a little of the picture through */
                SetLayeredWindowAttributes(s_win, KEY_COLOR, (BYTE)(s_theme.opacity * 255 / 100),
                                           LWA_COLORKEY | LWA_ALPHA);
                s_alpha_ver = s_theme_ver;
            }
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
