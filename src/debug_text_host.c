/*
 * debug_text_host.c - the game's on-screen debug text, drawn by the host.
 *
 * The retail build emptied DebugPrintf (0x161680, a bare ret), so the developers' debug menu and its pages
 * run but print nothing. src/recomp_manual.c formats each DebugPrintf and hands it here with the cursor the
 * game set through 0x18F5C0 (character cells, 0,0 = top left of the 640x480 picture). Text collects on a
 * character grid for one frame; the frame boundary is Render_SubmitFrame's own DebugPrintf (from 0x18EA4E).
 * A transparent, click-through popup owned by the D3D11 window shows the last complete frame over the 4:3
 * picture. Shift-JIS strings (some menu rows are Japanese) are converted with code page 932.
 *
 * Enabled with RECOMP_DEBUG_MENU=1 (the F9 debug menu) or RECOMP_DEBUG_TEXT=1 (any debug print the game
 * makes with a position). Unset: nothing here runs.
 */
#include "debug_text_host.h"
#include "pc_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#define GAME_CLASS "RecompNv2aD3D11"
#define DT_CLASS   "BreakdownDebugText"
#define COLS 80
#define ROWS 30
#define KEY_COLOR RGB(255, 0, 255)

static CRITICAL_SECTION s_lock;
static int s_on = -1;
static volatile int s_ready;   /* the lock exists (debug_text_host_start ran) */
static char s_work[ROWS][COLS + 1];
static char s_show[ROWS][COLS + 1];
static COLORREF s_wcol[ROWS][COLS], s_scol[ROWS][COLS];
static COLORREF s_color = RGB(250, 250, 250);
static int s_work_used, s_show_used;
static unsigned s_version;
static int s_col, s_row, s_left;
static HWND s_win;
static HFONT s_font;
static int s_font_h;

static int ready(void) { return s_ready; }

int debug_text_on(void)
{
    if (s_on < 0) {
        const char *a = getenv("RECOMP_DEBUG_MENU"), *b = getenv("RECOMP_DEBUG_TEXT");
        s_on = (a && *a == '1') || (b && *b == '1');
    }
    return s_on;
}

void debug_text_locate(int x, int y)
{
    if (!debug_text_on() || !ready())
        return;
    EnterCriticalSection(&s_lock);
    s_col = s_left = x;
    s_row = y;
    LeaveCriticalSection(&s_lock);
}

void debug_text_put(const char *s)
{
    if (!debug_text_on() || !ready() || !s)
        return;
    EnterCriticalSection(&s_lock);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n') {
            s_row++;
            s_col = s_left;
            continue;
        }
        if (c == '\t')
            c = ' ';
        else if (c < 0x20)
            continue;   /* control bytes take no cell */
        if (s_row >= 0 && s_row < ROWS && s_col >= 0 && s_col < COLS) {
            s_work[s_row][s_col] = (char)c;
            s_wcol[s_row][s_col] = s_color;
            s_work_used = 1;
        }
        s_col++;
    }
    LeaveCriticalSection(&s_lock);
}

void debug_text_frame_end(void)
{
    int r;
    if (!debug_text_on() || !ready())
        return;
    EnterCriticalSection(&s_lock);
    if (s_work_used != s_show_used || memcmp(s_work, s_show, sizeof s_work)) {
        memcpy(s_show, s_work, sizeof s_work);
        memcpy(s_scol, s_wcol, sizeof s_wcol);
        s_show_used = s_work_used;
        s_version++;
    }
    for (r = 0; r < ROWS; r++)
        memset(s_work[r], 0, sizeof s_work[r]);
    s_work_used = 0;
    s_color = RGB(250, 250, 250);
    LeaveCriticalSection(&s_lock);
}

void debug_text_color(int r, int g, int b)
{
    if (!debug_text_on() || !ready())
        return;
    EnterCriticalSection(&s_lock);
    s_color = RGB(r & 255, g & 255, b & 255);
    LeaveCriticalSection(&s_lock);
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(hwnd, &ps), mem;
    HBITMAP bmp, old_bmp;
    HBRUSH key;
    int w, h, r, cell_h;
    char rows[ROWS][COLS + 1];
    static COLORREF cols[ROWS][COLS];

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    if (w < 1 || h < 1) {
        EndPaint(hwnd, &ps);
        return;
    }
    EnterCriticalSection(&s_lock);
    memcpy(rows, s_show, sizeof rows);
    memcpy(cols, s_scol, sizeof cols);
    LeaveCriticalSection(&s_lock);

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);
    key = CreateSolidBrush(KEY_COLOR);
    FillRect(mem, &rc, key);
    DeleteObject(key);

    cell_h = h / ROWS;
    if (cell_h < 6)
        cell_h = 6;
    if (!s_font || s_font_h != cell_h) {
        if (s_font)
            DeleteObject(s_font);
        s_font = CreateFontA(-cell_h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "MS Gothic");
        s_font_h = cell_h;
    }
    SelectObject(mem, s_font);
    SetBkMode(mem, TRANSPARENT);
    for (r = 0; r < ROWS; r++) {
        int c = 0;
        while (c < COLS) {
            WCHAR wide[COLS + 1];
            int start, len, n;
            while (c < COLS && (rows[r][c] == 0 || rows[r][c] == ' '))
                c++;
            if (c >= COLS)
                break;
            start = c;
            while (c < COLS && rows[r][c] != 0 && cols[r][c] == cols[r][start])
                c++;
            len = c - start;
            n = MultiByteToWideChar(932, 0, &rows[r][start], len, wide, COLS);
            if (n <= 0)
                continue;
            {
                int x = start * w / COLS, y = r * h / ROWS;
                SetTextColor(mem, RGB(8, 8, 8));
                TextOutW(mem, x + 2, y + 2, wide, n);
                SetTextColor(mem, cols[r][start] == KEY_COLOR ? RGB(250, 250, 250) : cols[r][start]);
                TextOutW(mem, x, y, wide, n);
            }
        }
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK dt_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
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

/* Keep the popup over the game's 4:3 picture; show it while there is text. */
static void place(HWND game, unsigned *seen)
{
    RECT client;
    struct pc_rect fr;
    POINT o;
    int used;
    unsigned ver;
    if (!GetClientRect(game, &client))
        return;
    pc_options_frame_rect((unsigned)client.right, (unsigned)client.bottom, &fr);
    if (fr.w < 1 || fr.h < 1)
        return;
    o.x = fr.x;
    o.y = fr.y;
    ClientToScreen(game, &o);
    EnterCriticalSection(&s_lock);
    used = s_show_used;
    ver = s_version;
    LeaveCriticalSection(&s_lock);
    SetWindowPos(s_win, NULL, o.x, o.y, fr.w, fr.h,
                 SWP_NOZORDER | SWP_NOACTIVATE | (used ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (used && ver != *seen) {
        *seen = ver;
        InvalidateRect(s_win, NULL, FALSE);
    }
}

static DWORD WINAPI dt_thread(LPVOID unused)
{
    WNDCLASSA wc;
    HWND game = NULL;
    unsigned seen = 0;
    MSG m;
    (void)unused;
    while (!game) {
        game = FindWindowA(GAME_CLASS, NULL);
        if (!game)
            Sleep(250);
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = dt_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = DT_CLASS;
    RegisterClassA(&wc);
    s_win = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            DT_CLASS, "debug text", WS_POPUP, 0, 0, 10, 10, game, NULL, wc.hInstance, NULL);
    if (!s_win) {
        fprintf(stderr, "[DEBUG-TEXT] could not create the overlay (%lu)\n", GetLastError());
        return 0;
    }
    SetLayeredWindowAttributes(s_win, KEY_COLOR, 0, LWA_COLORKEY);
    SetTimer(s_win, 1, 33, NULL);
    fprintf(stderr, "[DEBUG-TEXT] overlay ready over the game window\n");
    fflush(stderr);
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            if (!IsWindow(game)) {
                HWND g = FindWindowA(GAME_CLASS, NULL);
                if (g)
                    game = g;
            }
            if (IsWindow(game))
                place(game, &seen);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

void debug_text_host_start(void)
{
    static int started;
    HANDLE t;
    if (started || !debug_text_on())
        return;
    started = 1;
    InitializeCriticalSection(&s_lock);
    s_ready = 1;
    t = CreateThread(NULL, 0, dt_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
