/*
 * tuning_host.c - the tuning range window over the running game (foster-game-dev game-tuning-range, phase 1).
 *
 * RECOMP_TUNING=1: F6 opens a tool window beside the game (movable, resizable, always on top). Rows show the value
 * the game uses now (read back from its memory), the game's default (first value seen this run), the finalized value,
 * min..max and ideal, coloured green (at ideal), yellow (in range) or red (out of range). Keys:
 *   Up/Down row, Left/Right change (Shift x10), I copy current -> ideal, D back to default, Tab next tab,
 *   P save preset, L load preset, F finalize (asks why), X export snapshot, Esc close.
 * Controller (src/tuning_pad.h): LS + RS click opens / closes it; D-pad up/down row, left/right change (hold RT x10),
 *   LB/RB tab, Y ideal, X default, B close, Start finalize -> A yes / B no. While it is open the game gets no buttons
 *   or triggers (sticks still work), see tuning_host_pad.
 * Files: <repo>/tuning/tuning_ranges.json, tuning_final.json, history/ and CHANGELOG.md (all in RECOMP_TUNING_DIR if set);
 * presets and snapshots in %LOCALAPPDATA%\xboxrecomp\tuning. The finalized file is applied in every build, window
 * or not: once per hero (when the hero's status block appears). Only the owner's F + Enter finalizes.
 *
 * Phase 1 rows (src/tuning.c): hero status block from src/recomp_manual.c (0x4FF20(hero+0x142)): life +0x5AB0,
 * max +0x5AB4, stamina +0x5AB8, max +0x5ABC; "Move speed x" stretches the hero's step (with the FastMove switch).
 */
#include "tuning_host.h"
#include "tuning.h"
#include "tuning_pad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

extern ptrdiff_t g_xbox_mem_offset;

#define GAME_CLASS "RecompNv2aD3D11"
#define TU_CLASS   "BreakdownTuningRange"
#define WM_TU_PAD  (WM_APP + 7)   /* wParam = TPA_ action, lParam = arg (from the game thread) */

static CRITICAL_SECTION s_lock;
static volatile int s_ready;
static int s_on = -1, s_have_final;
static volatile uint32_t s_st, s_applied_st;
static volatile float s_move = 1.0f;
static char s_dir[MAX_PATH], s_ranges[MAX_PATH], s_final[MAX_PATH], s_log[MAX_PATH], s_user[MAX_PATH];
static HWND volatile s_win;
static HWND s_edit, s_game;
static int s_tab, s_sel, s_prompt;
static char s_status[200] = "F6 or LS + RS click closes. Left/Right change, I (Y) = ideal, F (Start) = finalize.";

static float rf(uint32_t va) { return *(volatile float *)((uintptr_t)va + g_xbox_mem_offset); }
static void wf(uint32_t va, float v) { *(volatile float *)((uintptr_t)va + g_xbox_mem_offset) = v; }

static int on(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_TUNING");
        s_on = e && *e == '1';
    }
    return s_on;
}

int tuning_host_active(void) { return s_ready && (on() || s_have_final); }
float tuning_host_move_speed(void) { return s_move; }

/* ---- live values ---- */

static int live_read(const struct tune_row *r, uint32_t st, float *v)
{
    if (strcmp(r->key, "player.move_speed") == 0) { *v = s_move; return 1; }
    if (!st)
        return 0;
    if (strcmp(r->key, "player.max_life") == 0) { *v = rf(st + 0x5AB4); return 1; }
    if (strcmp(r->key, "player.life") == 0) { *v = rf(st + 0x5AB0); return 1; }
    if (strcmp(r->key, "player.max_stamina") == 0) { *v = rf(st + 0x5ABC); return 1; }
    if (strcmp(r->key, "player.stamina") == 0) { *v = rf(st + 0x5AB8); return 1; }
    return 0;
}

static void live_write(const struct tune_row *r, uint32_t st, float v)
{
    if (strcmp(r->key, "player.move_speed") == 0) { s_move = v; return; }
    if (!st)
        return;
    if (strcmp(r->key, "player.max_life") == 0) {
        float life = rf(st + 0x5AB0), max = rf(st + 0x5AB4);
        wf(st + 0x5AB4, v);
        if (life > v || fabsf(life - max) < 1e-3f)   /* full stays full; never above max */
            wf(st + 0x5AB0, v);
    } else if (strcmp(r->key, "player.life") == 0) {
        wf(st + 0x5AB0, v > rf(st + 0x5AB4) ? rf(st + 0x5AB4) : v);
    } else if (strcmp(r->key, "player.max_stamina") == 0) {
        float sta = rf(st + 0x5AB8), max = rf(st + 0x5ABC);
        wf(st + 0x5ABC, v);
        if (sta > v || fabsf(sta - max) < 1e-3f)
            wf(st + 0x5AB8, v);
    } else if (strcmp(r->key, "player.stamina") == 0) {
        wf(st + 0x5AB8, v > rf(st + 0x5ABC) ? rf(st + 0x5ABC) : v);
    }
}

static void observe_all(uint32_t st)
{
    int i;
    for (i = 0; i < tuning_count(); i++) {
        float v;
        if (live_read(tuning_row(i), st, &v))
            tuning_observe(i, v);
    }
}

/* Game thread, every hero update. A new status block = a new hero (load, continue): note the game's own values
 * (defaults), then apply the finalized ones. */
void tuning_host_hero(uint32_t st)
{
    int i;
    if (!s_ready)
        return;
    s_st = st;
    if (!st || st == s_applied_st)
        return;
    EnterCriticalSection(&s_lock);
    observe_all(st);
    for (i = 0; i < tuning_count(); i++) {
        struct tune_row *r = tuning_row(i);
        if (r->have_fin && !r->state && strcmp(r->key, "player.move_speed") != 0) {
            live_write(r, st, tuning_set(i, r->fin));
            fprintf(stderr, "[TUNING] %s = %g (finalized; game %g)\n", r->key, r->fin, r->def);
        }
    }
    s_applied_st = st;
    LeaveCriticalSection(&s_lock);
    fflush(stderr);
}

/* ---- files ---- */

static void paths(void)
{
    char exe[MAX_PATH], *p;
    const char *d = getenv("RECOMP_TUNING_DIR"), *la = getenv("LOCALAPPDATA");
    GetModuleFileNameA(NULL, exe, sizeof exe);
    if ((p = strrchr(exe, '\\')) != NULL)
        *p = 0;                              /* build-win */
    if ((p = strrchr(exe, '\\')) != NULL)
        *p = 0;                              /* the repo */
    if (d && *d)
        snprintf(s_dir, sizeof s_dir, "%s", d);
    else
        snprintf(s_dir, sizeof s_dir, "%s\\tuning", exe);
    snprintf(s_ranges, sizeof s_ranges, "%s\\tuning_ranges.json", s_dir);
    snprintf(s_final, sizeof s_final, "%s\\tuning_final.json", s_dir);
    snprintf(s_log, sizeof s_log, "%s\\CHANGELOG.md", s_dir);
    snprintf(s_user, sizeof s_user, "%s\\xboxrecomp\\tuning", la ? la : ".");
}

static void now_str(char *out, size_t n, int file)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_s(&tm, &t);
    strftime(out, n, file ? "%Y-%m-%d_%H%M" : "%Y-%m-%d %H:%M", &tm);
}

static void status(const char *fmt, const char *a)
{
    snprintf(s_status, sizeof s_status, fmt, a ? a : "");
}

/* ---- window ---- */

static int row_visible(int i)
{
    const struct tune_row *r = tuning_row(i);
    if (s_tab == TUNE_TAB_FINALIZED)
        return r->have_fin;
    return s_tab == TUNE_TAB_SNAPSHOT || r->tab == s_tab;
}

static int visible_index(int k)   /* k-th visible row -> row index */
{
    int i, n = 0;
    for (i = 0; i < tuning_count(); i++)
        if (row_visible(i) && n++ == k)
            return i;
    return -1;
}

static int visible_count(void)
{
    int i, n = 0;
    for (i = 0; i < tuning_count(); i++)
        n += row_visible(i);
    return n;
}

static void fmtv(char *b, size_t n, int have, float v)
{
    if (have)
        snprintf(b, n, "%.2f", v);
    else
        snprintf(b, n, "-");
}

static void paint(HWND hwnd, HDC dc)
{
    static const char *const tabs[TUNE_TAB_COUNT] = { "Player", "Snapshot", "Finalized" };
    static const COLORREF col[] = { RGB(80, 200, 110), RGB(230, 200, 70), RGB(230, 80, 70), RGB(120, 120, 130) };
    RECT rc;
    HBRUSH bg = CreateSolidBrush(RGB(14, 18, 28)), sel = CreateSolidBrush(RGB(36, 60, 96));
    HFONT f = CreateFontA(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT old;
    int i, k, x, y = 8, n;
    char b[160];
    static const int colx[] = { 12, 190, 290, 390, 490, 640, 730 };
    static const char *const head[] = { "Value", "Current", "Default", "Finalized", "Min .. Max", "Ideal", "vs final" };
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, bg);
    old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    for (i = 0, x = 12; i < TUNE_TAB_COUNT; i++) {
        SetTextColor(dc, i == s_tab ? RGB(255, 230, 120) : RGB(150, 160, 180));
        snprintf(b, sizeof b, i == s_tab ? "[%s]" : "%s", tabs[i]);
        TextOutA(dc, x, y, b, (int)strlen(b));
        x += 110;
    }
    y += 30;
    SetTextColor(dc, RGB(150, 160, 180));
    for (i = 0; i < 7; i++)
        TextOutA(dc, colx[i], y, head[i], (int)strlen(head[i]));
    y += 24;
    EnterCriticalSection(&s_lock);
    n = visible_count();
    if (s_sel >= n)
        s_sel = n ? n - 1 : 0;
    for (k = 0; k < n; k++, y += 24) {
        struct tune_row *r = tuning_row(visible_index(k));
        int c = tuning_colour(visible_index(k)), have;
        float d;
        if (k == s_sel) {
            RECT hr = { 4, y - 2, rc.right - 4, y + 22 };
            FillRect(dc, &hr, sel);
        }
        SetTextColor(dc, col[c]);
        TextOutA(dc, colx[0], y, "\x95", 1);
        SetTextColor(dc, RGB(230, 232, 240));
        snprintf(b, sizeof b, "%s%s", r->name, r->state ? " (state)" : "");
        TextOutA(dc, colx[0] + 14, y, b, (int)strlen(b));
        fmtv(b, sizeof b, r->have_cur, r->cur); TextOutA(dc, colx[1], y, b, (int)strlen(b));
        fmtv(b, sizeof b, r->have_def, r->def); TextOutA(dc, colx[2], y, b, (int)strlen(b));
        fmtv(b, sizeof b, r->have_fin, r->fin); TextOutA(dc, colx[3], y, b, (int)strlen(b));
        if (r->have_range)
            snprintf(b, sizeof b, "%.2f .. %.2f", r->min, r->max);
        else
            snprintf(b, sizeof b, "-");
        TextOutA(dc, colx[4], y, b, (int)strlen(b));
        fmtv(b, sizeof b, r->have_ideal, r->ideal); TextOutA(dc, colx[5], y, b, (int)strlen(b));
        d = tuning_delta_pct(visible_index(k), &have);
        if (have)
            snprintf(b, sizeof b, "%+.0f%%", d);
        else
            snprintf(b, sizeof b, "-");
        TextOutA(dc, colx[6], y, b, (int)strlen(b));
    }
    LeaveCriticalSection(&s_lock);
    if (!n) {
        SetTextColor(dc, RGB(150, 160, 180));
        snprintf(b, sizeof b, s_tab == TUNE_TAB_FINALIZED ? "Nothing finalized yet." : "Load a save: the values appear once the hero is in play.");
        TextOutA(dc, 12, y, b, (int)strlen(b));
    }
    SetTextColor(dc, RGB(150, 160, 180));
    snprintf(b, sizeof b, "Up/Down row  Left/Right change (Shift x10)  I ideal  D default  Tab tab  P/L preset  F finalize  X snapshot  Esc close");
    TextOutA(dc, 12, rc.bottom - 76, b, (int)strlen(b));
    snprintf(b, sizeof b, "Pad: D-pad row / change (hold RT x10)  LB/RB tab  Y ideal  X default  Start finalize  B or LS+RS close");
    TextOutA(dc, 12, rc.bottom - 52, b, (int)strlen(b));
    SetTextColor(dc, RGB(255, 230, 120));
    TextOutA(dc, 12, rc.bottom - 28, s_status, (int)strlen(s_status));
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteObject(bg);
    DeleteObject(sel);
}

static void close_window(void)
{
    ShowWindow(s_win, SW_HIDE);
    if (s_game && IsWindow(s_game))
        SetForegroundWindow(s_game);
}

static void finalize_now(const char *fallback)
{
    char note[200] = "", date[32];
    int n;
    GetWindowTextA(s_edit, note, sizeof note);
    if (!note[0] && fallback)
        snprintf(note, sizeof note, "%s", fallback);
    now_str(date, sizeof date, 0);
    CreateDirectoryA(s_dir, NULL);
    EnterCriticalSection(&s_lock);
    n = tuning_finalize(s_dir, s_log, date, note);
    LeaveCriticalSection(&s_lock);
    if (n < 0)
        status("Finalize failed: could not write %s", s_final);
    else {
        char m[64];
        snprintf(m, sizeof m, "%d", n);
        status("Finalized %s changed values (tuning_final.json, history, CHANGELOG.md).", m);
        s_have_final = n > 0;
        fprintf(stderr, "[TUNING] finalized %d values: %s\n", n, note);
        fflush(stderr);
    }
    s_prompt = 0;
    ShowWindow(s_edit, SW_HIDE);
    SetFocus(s_win);
}

static void open_window(void)
{
    static int s_placed;
    RECT gr;
    if (!s_placed && GetWindowRect(s_game, &gr)) {   /* first open: over the game's top-left */
        SetWindowPos(s_win, NULL, gr.left + 24, gr.top + 48, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        s_placed = 1;
    }
    ShowWindow(s_win, SW_SHOW);
    SetForegroundWindow(s_win);
    SetFocus(s_win);
}

static void toggle_window(void)
{
    if (IsWindowVisible(s_win))
        close_window();
    else
        open_window();
}

static void move_tab(int d) { s_tab = (s_tab + TUNE_TAB_COUNT + d) % TUNE_TAB_COUNT; s_sel = 0; }

static void move_row(int d)
{
    int n = visible_count();
    if (d < 0 && s_sel > 0)
        s_sel--;
    if (d > 0 && s_sel + 1 < n)
        s_sel++;
}

/* The selected row: change by steps, back to default, or current -> ideal (saved in tuning_ranges.json). */
static void row_change(int steps)
{
    int i = visible_index(s_sel);
    struct tune_row *r;
    if (i < 0)
        return;
    r = tuning_row(i);
    EnterCriticalSection(&s_lock);
    if (r->have_cur)
        live_write(r, s_st, tuning_nudge(i, steps));
    LeaveCriticalSection(&s_lock);
}

static void row_default(void)
{
    int i = visible_index(s_sel);
    struct tune_row *r;
    if (i < 0)
        return;
    r = tuning_row(i);
    EnterCriticalSection(&s_lock);
    if (r->have_def)
        live_write(r, s_st, tuning_set(i, r->def));
    LeaveCriticalSection(&s_lock);
}

static void row_ideal(void)
{
    int i = visible_index(s_sel);
    struct tune_row *r;
    if (i < 0)
        return;
    r = tuning_row(i);
    EnterCriticalSection(&s_lock);
    if (r->have_cur) {
        r->ideal = r->cur;
        r->have_ideal = 1;
        CreateDirectoryA(s_dir, NULL);
        status(tuning_save_ranges(s_ranges) == 1 ? "Ideal set and saved in %s" : "Could not save %s", s_ranges);
    }
    LeaveCriticalSection(&s_lock);
}

/* Rows Finalize would write (non-state, Current differs from Default). */
static int changed_count(void)
{
    int i, n = 0;
    EnterCriticalSection(&s_lock);
    for (i = 0; i < tuning_count(); i++) {
        const struct tune_row *r = tuning_row(i);
        n += !r->state && r->have_cur && r->have_def && fabsf(r->cur - r->def) > 1e-6f;
    }
    LeaveCriticalSection(&s_lock);
    return n;
}

/* One controller action, posted by tuning_host_pad. Only the owner's A after Start finalizes. */
static void pad_action(int what, int arg)
{
    char m[16];
    switch (what) {
    case TPA_TOGGLE: toggle_window(); break;
    case TPA_CLOSE: close_window(); break;
    case TPA_ROW: move_row(arg); break;
    case TPA_TAB: move_tab(arg); break;
    case TPA_CHANGE: row_change(arg); break;
    case TPA_DEFAULT: row_default(); break;
    case TPA_IDEAL: row_ideal(); break;
    case TPA_ASK_FINAL:
        snprintf(m, sizeof m, "%d", changed_count());
        status("Finalize %s changed values? A = yes, B = no.", m);
        break;
    case TPA_FINAL_YES: finalize_now("finalized from the controller"); break;
    case TPA_FINAL_NO: status("Finalize cancelled.%s", ""); break;
    }
}

static void key(WPARAM vk)
{
    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, i;
    char path[MAX_PATH], date[32];
    if (vk == VK_ESCAPE) { close_window(); return; }
    if (vk == VK_TAB) { move_tab(1); return; }
    if (vk == VK_UP || vk == VK_DOWN) { move_row(vk == VK_DOWN ? 1 : -1); return; }
    if (vk == 'P' || vk == 'L') {
        CreateDirectoryA(s_user, NULL);
        snprintf(path, sizeof path, "%s\\preset.json", s_user);
        EnterCriticalSection(&s_lock);
        if (vk == 'P')
            status(tuning_save_values(path) == 1 ? "Preset saved: %s" : "Could not save %s", path);
        else if (tuning_load_preset(path) == 1) {
            for (i = 0; i < tuning_count(); i++)
                if (!tuning_row(i)->state && tuning_row(i)->have_cur)
                    live_write(tuning_row(i), s_st, tuning_row(i)->cur);
            status("Preset loaded: %s", path);
        } else
            status("No preset yet (%s)", path);
        LeaveCriticalSection(&s_lock);
        return;
    }
    if (vk == 'X') {
        CreateDirectoryA(s_user, NULL);
        now_str(date, sizeof date, 1);
        snprintf(path, sizeof path, "%s\\snapshot-%s.md", s_user, date);
        EnterCriticalSection(&s_lock);
        status(tuning_snapshot(path, date) == 1 ? "Snapshot: %s" : "Could not write %s", path);
        LeaveCriticalSection(&s_lock);
        return;
    }
    if (vk == 'F') {
        s_prompt = 1;
        SetWindowTextA(s_edit, "");
        ShowWindow(s_edit, SW_SHOW);
        SetFocus(s_edit);
        status("Finalize: why? Type a note, Enter to finalize, Esc to cancel.%s", "");
        return;
    }
    if (vk == VK_LEFT || vk == VK_RIGHT)
        row_change((vk == VK_RIGHT ? 1 : -1) * (shift ? 10 : 1));
    else if (vk == 'D')
        row_default();
    else if (vk == 'I')
        row_ideal();
}

static WNDPROC s_edit_proc;
static LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { finalize_now(NULL); InvalidateRect(s_win, NULL, FALSE); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        s_prompt = 0;
        ShowWindow(h, SW_HIDE);
        SetFocus(s_win);
        status("Finalize cancelled.%s", "");
        InvalidateRect(s_win, NULL, FALSE);
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27))
        return 0;
    return CallWindowProcA(s_edit_proc, h, msg, wp, lp);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hwnd, &ps), mem;
        HBITMAP bmp, old;
        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = (HBITMAP)SelectObject(mem, bmp);
        paint(hwnd, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (s_edit)
            MoveWindow(s_edit, 12, HIWORD(lp) - 108, LOWORD(lp) - 24, 24, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (!s_prompt) {
            key(wp);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_CLOSE:
        close_window();
        return 0;
    case WM_TU_PAD:
        pad_action((int)wp, (int)lp);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI tu_thread(LPVOID unused)
{
    WNDCLASSA wc;
    MSG m;
    int was6 = 0;
    (void)unused;
    while (!s_game) {
        s_game = FindWindowA(GAME_CLASS, NULL);
        if (!s_game)
            Sleep(250);
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = TU_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    s_win = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TU_CLASS, "Breakdown tuning range",
                            WS_OVERLAPPEDWINDOW, 60, 60, 840, 380, s_game, NULL, wc.hInstance, NULL);
    if (!s_win)
        return 0;
    s_edit = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 12, 260, 800, 24, s_win, NULL,
                             wc.hInstance, NULL);
    s_edit_proc = (WNDPROC)SetWindowLongPtrA(s_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    SetTimer(s_win, 1, 100, NULL);
    fprintf(stderr, "[TUNING] window ready: F6 opens it (files in %s)\n", s_dir);
    fflush(stderr);
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER && m.hwnd == s_win) {
            int d6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            HWND fg = GetForegroundWindow();
            if (d6 && !was6 && (fg == s_game || fg == s_win))
                toggle_window();
            was6 = d6;
            if (IsWindowVisible(s_win)) {
                EnterCriticalSection(&s_lock);
                observe_all(s_st);
                LeaveCriticalSection(&s_lock);
                InvalidateRect(s_win, NULL, FALSE);
            }
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

/* Game thread, every XInputGetState (src/recomp_manual.c). Reads the pad for the window and hides from the game
 * what the window uses: every button and trigger while it is open, then anything still held until let go.
 * blocked = the PC options page has the pad. The pad only drives the window while the game or the window has the
 * focus. */
void tuning_host_pad(int port, uint16_t *wbuttons, uint8_t analog[8], int blocked)
{
    struct tp_result r;
    HWND w = s_win, fg;
    unsigned bits;
    int i;
    if (!s_ready || !on() || !w)
        return;
    fg = GetForegroundWindow();
    bits = (blocked || (fg != s_game && fg != w)) ? 0 : tp_bits(*wbuttons, analog);
    r = tp_step(port, bits, IsWindowVisible(w), GetTickCount());
    for (i = 0; i < r.n; i++)
        PostMessageA(w, WM_TU_PAD, (WPARAM)r.act[i].what, (LPARAM)r.act[i].arg);
    *wbuttons = (uint16_t)(*wbuttons & ~(r.strip & 0xFFu));
    for (i = 0; i < 8; i++)
        if (r.strip & (0x100u << i))
            analog[i] = 0;
}

void tuning_host_start(void)
{
    static int started;
    int i, rr, rf2;
    if (started)
        return;
    started = 1;
    InitializeCriticalSection(&s_lock);
    tuning_reset();
    paths();
    rr = tuning_load_ranges(s_ranges);
    rf2 = tuning_load_final(s_final);
    for (i = 0; i < tuning_count(); i++)
        if (tuning_row(i)->have_fin) {
            s_have_final = 1;
            if (strcmp(tuning_row(i)->key, "player.move_speed") == 0)
                s_move = tuning_row(i)->fin;
        }
    tuning_observe(tuning_find("player.move_speed"), 1.0f);   /* the game's own speed is the default */
    tuning_set(tuning_find("player.move_speed"), s_move);
    s_ready = 1;
    if (rr < 0 || rf2 < 0)
        fprintf(stderr, "[TUNING] could not parse %s or %s\n", s_ranges, s_final);
    if (s_have_final)
        fprintf(stderr, "[TUNING] finalized values loaded from %s\n", s_final);
    if (on()) {
        HANDLE t = CreateThread(NULL, 0, tu_thread, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
    }
    fflush(stderr);
}
