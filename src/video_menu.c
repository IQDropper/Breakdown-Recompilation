/*
 * video_menu.c - in-game Video options (host window, F10).
 *
 * Scale and the present filter are queued with nv2a_host_opts_request and
 * the D3D11 backend applies them on the next flip. Window size, letterbox
 * aspect, and borderless versus windowed are applied by resizing that
 * window (WM_SIZE updates the swap chain). Exclusive fullscreen and hor+
 * are not on this path. Nothing here is linked into toolkit smoke tests.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "video_menu.h"
#include "video_config.h"

#include "kernel/nv2a_backend_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_CLASS "BreakdownVideoMenu"
#define GAME_CLASS "RecompNv2aD3D11"

enum {
    MENU_W = 640,
    MENU_TOP = 112,
    MENU_ROW = 34,
    MENU_BOTTOM = 124
};

static const COLORREF COL_BG = RGB(22, 24, 28);
static const COLORREF COL_ROW = RGB(36, 52, 74);
static const COLORREF COL_TEXT = RGB(232, 234, 238);
static const COLORREF COL_DIM = RGB(168, 172, 180);
static const COLORREF COL_VALUE = RGB(186, 214, 255);

static HWND s_game;
static HWND s_menu;
static WNDPROC s_old;
static HFONT s_font;
static HFONT s_title_font;
static int s_started;
static int s_open;
static int s_row;
static int s_have_session;
static int s_have_place;
static int s_env_override;
static int s_client_w = MENU_W;
static struct video_settings s_draft;
static struct video_settings s_committed;
static struct video_settings s_session;
static WINDOWPLACEMENT s_place;
static char s_status[192];

static char b_scale[8];
static char b_window[8];
static char b_aspect[8];
static char b_filter[12];
static char b_full[4];

static int env_nonempty(const char *key)
{
    const char *v = getenv(key);
    return v && v[0];
}

static int any_env_override(void)
{
    return env_nonempty("RECOMP_RENDER_SCALE")
        || env_nonempty("RECOMP_WINDOW_SCALE")
        || env_nonempty("RECOMP_DISPLAY_ASPECT")
        || env_nonempty("RECOMP_PRESENT_FILTER")
        || env_nonempty("RECOMP_PRESENT_FULLSCREEN");
}

static char *bound_get(const char *name)
{
    if (strcmp(name, "RECOMP_RENDER_SCALE") == 0)
        return b_scale;
    if (strcmp(name, "RECOMP_WINDOW_SCALE") == 0)
        return b_window;
    if (strcmp(name, "RECOMP_DISPLAY_ASPECT") == 0)
        return b_aspect;
    if (strcmp(name, "RECOMP_PRESENT_FILTER") == 0)
        return b_filter;
    if (strcmp(name, "RECOMP_PRESENT_FULLSCREEN") == 0)
        return b_full;
    return NULL;
}

static void bind_settings(const struct video_settings *s)
{
    struct video_settings c = *s;
    video_settings_clamp(&c);
    snprintf(b_scale, sizeof b_scale, "%u", c.render_scale);
    snprintf(b_window, sizeof b_window, "%u", c.window_scale);
    snprintf(b_aspect, sizeof b_aspect, "%s", video_settings_aspect_name(c.aspect));
    snprintf(b_filter, sizeof b_filter, "%s", video_settings_filter_name(c.filter));
    snprintf(b_full, sizeof b_full, "%d", c.borderless ? 1 : 0);
}

static int style_is_borderless(HWND h)
{
    LONG_PTR style = GetWindowLongPtrA(h, GWL_STYLE);
    return (style & WS_POPUP) && !(style & WS_CAPTION);
}

static void place_menu(void)
{
    RECT game, menu, work;
    HMONITOR mon;
    MONITORINFO mi;
    int w, h, x, y;

    if (!s_menu || !s_game || !IsWindow(s_menu) || !IsWindow(s_game))
        return;
    if (!GetWindowRect(s_game, &game) || !GetWindowRect(s_menu, &menu))
        return;
    w = menu.right - menu.left;
    h = menu.bottom - menu.top;
    x = game.left + ((game.right - game.left) - w) / 2;
    y = game.top + ((game.bottom - game.top) - h) / 2;
    mon = MonitorFromWindow(s_game, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoA(mon, &mi)) {
        work = mi.rcWork;
        if (x < work.left)
            x = work.left;
        if (y < work.top)
            y = work.top;
        if (x + w > work.right)
            x = work.right - w;
        if (y + h > work.bottom)
            y = work.bottom - h;
    }
    SetWindowPos(s_menu, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

static void windowed_origin(int was_borderless, int w, int h, int *x, int *y)
{
    MONITORINFO mi;
    HMONITOR mon;
    RECT outer;

    if (was_borderless && s_have_place) {
        *x = s_place.rcNormalPosition.left;
        *y = s_place.rcNormalPosition.top;
        return;
    }
    if (!was_borderless && GetWindowRect(s_game, &outer)) {
        *x = outer.left;
        *y = outer.top;
        return;
    }
    *x = 80;
    *y = 60;
    mon = MonitorFromWindow(s_game, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoA(mon, &mi)) {
        *x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - w) / 2;
        *y = mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - h) / 2;
    }
}

static void resize_game(const struct display_choice *c)
{
    DWORD style;
    RECT frame;
    int was_borderless, x, y, w, h;

    if (!s_game || !IsWindow(s_game) || !c)
        return;
    was_borderless = style_is_borderless(s_game);
    if (c->fullscreen) {
        MONITORINFO mi;
        HMONITOR mon;
        if (!was_borderless) {
            s_place.length = sizeof s_place;
            if (GetWindowPlacement(s_game, &s_place))
                s_have_place = 1;
        }
        mon = MonitorFromWindow(s_game, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoA(mon, &mi)) {
            mi.rcMonitor.left = 0;
            mi.rcMonitor.top = 0;
            mi.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
            mi.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        SetWindowLongPtrA(s_game, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(s_game, NULL,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
        place_menu();
        return;
    }

    style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    frame.left = 0;
    frame.top = 0;
    frame.right = (LONG)c->window_w;
    frame.bottom = (LONG)c->window_h;
    AdjustWindowRect(&frame, style, FALSE);
    w = frame.right - frame.left;
    h = frame.bottom - frame.top;
    windowed_origin(was_borderless, w, h, &x, &y);
    SetWindowLongPtrA(s_game, GWL_STYLE, (LONG_PTR)style);
    SetWindowPos(s_game, NULL, x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
    place_menu();
}

static void request_opts(const struct display_choice *c)
{
    struct nv2a_host_opts o;
    memset(&o, 0, sizeof o);
    o.render_scale = c->render_scale;
    o.present_filter = c->present_filter;
    o.fullscreen = c->fullscreen;
    nv2a_host_opts_request(&o, NV2A_OPT_SCALE | NV2A_OPT_FILTER | NV2A_OPT_FULLSCREEN);
    fprintf(stderr,
            "[VIDEO] apply render.scale=%u filter=%s borderless=%d window=%ux%u aspect=%s\n",
            c->render_scale, c->filter_name, c->fullscreen, c->window_w, c->window_h, c->aspect);
}

/* restyle 0 leaves the HWND alone (scale and filter still queue). */
static int apply_live(const struct video_settings *s, int restyle)
{
    struct display_choice c;
    int have = s_game && IsWindow(s_game);

    bind_settings(s);
    display_host_choose(bound_get, &c);
    request_opts(&c);
    if (restyle && have)
        resize_game(&c);
    return have;
}

static int save_draft(void)
{
    char path[512];

    if (video_config_beside_exe(path, sizeof path) != 0) {
        snprintf(s_status, sizeof s_status, "Could not find the folder beside the exe.");
        return 0;
    }
    if (video_config_save(path, &s_draft) != 0) {
        snprintf(s_status, sizeof s_status, "Could not save %s.", path);
        return 0;
    }
    s_committed = s_draft;
    s_session = s_draft;
    s_have_session = 1;
    snprintf(s_status, sizeof s_status, "Saved %s", path);
    fprintf(stderr, "[VIDEO] saved %s\n", path);
    return 1;
}

static void hide_menu(int revert)
{
    if (revert) {
        s_draft = s_committed;
        s_session = s_committed;
        s_have_session = 1;
        if (!apply_live(&s_committed, 1))
            snprintf(s_status, sizeof s_status,
                     "Reverted. Window size and borderless apply on the next launch.");
    }
    s_open = 0;
    if (s_menu)
        ShowWindow(s_menu, SW_HIDE);
    if (s_game && IsWindow(s_game))
        SetFocus(s_game);
}

static void activate_row(void)
{
    if (s_row == VIDEO_ROW_APPLY) {
        save_draft();
        if (s_menu)
            InvalidateRect(s_menu, NULL, FALSE);
        return;
    }
    if (s_row == VIDEO_ROW_OK) {
        if (save_draft())
            hide_menu(0);
        else if (s_menu)
            InvalidateRect(s_menu, NULL, FALSE);
        return;
    }
    if (s_row == VIDEO_ROW_CANCEL)
        hide_menu(1);
}

static void preview(int dir)
{
    if (!video_settings_nudge(&s_draft, s_row, dir))
        return;
    s_session = s_draft;
    s_have_session = 1;
    if (!apply_live(&s_draft, video_row_needs_window(s_row)))
        snprintf(s_status, sizeof s_status,
                 "No D3D11 window. Scale and filter still queue; window size and borderless apply on the next launch.");
    else if (video_row_needs_window(s_row))
        snprintf(s_status, sizeof s_status, "Resized now. Apply or OK writes the file.");
    else
        snprintf(s_status, sizeof s_status, "Next frame. Apply or OK writes the file.");
    if (s_menu)
        InvalidateRect(s_menu, NULL, FALSE);
}

static void move_row(int dir)
{
    s_row += dir;
    if (s_row < 0)
        s_row = VIDEO_ROW_COUNT - 1;
    if (s_row >= VIDEO_ROW_COUNT)
        s_row = 0;
    if (s_menu)
        InvalidateRect(s_menu, NULL, FALSE);
}

static void seed_session(void)
{
    struct display_choice c;
    if (s_have_session)
        return;
    display_host_choose(display_getenv_layered, &c);
    video_settings_from_choice(&c, &s_session);
    s_have_session = 1;
}

static void paint_menu(HDC hdc, const RECT *rc)
{
    RECT row, text, block;
    HBRUSH bg, hi;
    int i, y, bottom;
    char value[32];

    s_client_w = rc->right - rc->left;
    bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, rc, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);

    SelectObject(hdc, s_title_font ? s_title_font : s_font);
    SetTextColor(hdc, COL_TEXT);
    text.left = 18;
    text.top = 12;
    text.right = rc->right - 18;
    text.bottom = 40;
    DrawTextA(hdc, "Video", -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, s_font);
    SetTextColor(hdc, COL_DIM);
    text.top = 42;
    text.bottom = 62;
    DrawTextA(hdc, "Host options. The game frame stays 4:3.", -1, &text,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    text.top = 62;
    text.bottom = 82;
    DrawTextA(hdc, "Hor+ and exclusive fullscreen are not available.", -1, &text,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (s_env_override) {
        text.top = 82;
        text.bottom = 104;
        DrawTextA(hdc, "A RECOMP_* variable is set. It overrides this file on the next launch.",
                  -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    hi = CreateSolidBrush(COL_ROW);
    for (i = 0; i < VIDEO_ROW_COUNT; i++) {
        y = MENU_TOP + i * MENU_ROW;
        row.left = 10;
        row.top = y;
        row.right = rc->right - 10;
        row.bottom = y + MENU_ROW - 2;
        if (i == s_row)
            FillRect(hdc, &row, hi);
        SetTextColor(hdc, i == s_row ? COL_TEXT : COL_DIM);
        text = row;
        text.left = 22;
        text.right = rc->right - 180;
        DrawTextA(hdc, video_row_title(i), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (i < VIDEO_ROW_APPLY) {
            video_settings_value(&s_draft, i, value, sizeof value);
            SetTextColor(hdc, COL_VALUE);
            text.left = rc->right - 168;
            text.right = rc->right - 18;
            DrawTextA(hdc, "<", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hdc, value, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hdc, ">", -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    DeleteObject(hi);

    bottom = rc->bottom;
    SetTextColor(hdc, COL_DIM);
    block.left = 18;
    block.right = rc->right - 18;
    block.top = MENU_TOP + VIDEO_ROW_COUNT * MENU_ROW + 6;
    block.bottom = bottom - 56;
    DrawTextA(hdc, video_row_detail(&s_draft, s_row), -1, &block,
              DT_LEFT | DT_WORDBREAK | DT_TOP);
    text.left = 18;
    text.right = rc->right - 18;
    text.top = bottom - 54;
    text.bottom = bottom - 36;
    DrawTextA(hdc, "Left/Right preview. Apply or OK saves. F10 or Esc cancels.",
              -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SetTextColor(hdc, COL_TEXT);
    text.top = bottom - 34;
    text.bottom = bottom - 10;
    DrawTextA(hdc, s_status, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void on_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc, mem;
    HBITMAP bmp, old;
    RECT rc;
    int w, hgt;

    hdc = BeginPaint(h, &ps);
    GetClientRect(h, &rc);
    w = rc.right - rc.left;
    hgt = rc.bottom - rc.top;
    mem = CreateCompatibleDC(hdc);
    bmp = (w > 0 && hgt > 0) ? CreateCompatibleBitmap(hdc, w, hgt) : NULL;
    if (!mem || !bmp) {
        if (bmp)
            DeleteObject(bmp);
        if (mem)
            DeleteDC(mem);
        paint_menu(hdc, &rc);
        EndPaint(h, &ps);
        return;
    }
    old = (HBITMAP)SelectObject(mem, bmp);
    paint_menu(mem, &rc);
    BitBlt(hdc, 0, 0, w, hgt, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static void on_click(int x, int y)
{
    int row, rel, box;

    if (y < MENU_TOP)
        return;
    row = (y - MENU_TOP) / MENU_ROW;
    if (row < 0 || row >= VIDEO_ROW_COUNT)
        return;
    s_row = row;
    if (row >= VIDEO_ROW_APPLY) {
        activate_row();
        return;
    }
    box = s_client_w - 168;
    rel = x - box;
    if (rel >= 0 && rel < 36)
        preview(-1);
    else if (rel >= 120)
        preview(1);
    else if (s_menu)
        InvalidateRect(s_menu, NULL, FALSE);
}

static LRESULT CALLBACK menu_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_PAINT:
        on_paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w == VK_F10 && (l & (1L << 30)))
            return 0;
        if (w == VK_ESCAPE || w == VK_F10) {
            hide_menu(1);
            return 0;
        }
        if (w == VK_UP) {
            move_row(-1);
            return 0;
        }
        if (w == VK_DOWN) {
            move_row(1);
            return 0;
        }
        if (w == VK_LEFT) {
            preview(-1);
            return 0;
        }
        if (w == VK_RIGHT) {
            preview(1);
            return 0;
        }
        if (w == VK_RETURN) {
            activate_row();
            return 0;
        }
        return 0;
    case WM_LBUTTONDOWN:
        on_click((int)(short)LOWORD(l), (int)(short)HIWORD(l));
        return 0;
    case WM_MOUSEWHEEL:
        if ((short)HIWORD(w) > 0)
            preview(1);
        else if ((short)HIWORD(w) < 0)
            preview(-1);
        return 0;
    case WM_CLOSE:
        hide_menu(1);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcA(h, m, w, l);
}

static void ensure_menu(void)
{
    WNDCLASSA wc;
    RECT frame;
    DWORD style, ex;
    int w, h;

    if (s_menu)
        return;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = menu_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = MENU_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);
    if (!s_font)
        s_font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    if (!s_font)
        s_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!s_title_font)
        s_title_font = CreateFontA(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    if (!s_title_font)
        s_title_font = s_font;
    style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    ex = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
    frame.left = 0;
    frame.top = 0;
    frame.right = MENU_W;
    frame.bottom = MENU_TOP + VIDEO_ROW_COUNT * MENU_ROW + MENU_BOTTOM;
    AdjustWindowRectEx(&frame, style, FALSE, ex);
    w = frame.right - frame.left;
    h = frame.bottom - frame.top;
    s_menu = CreateWindowExA(ex, MENU_CLASS, "Video", style,
                             0, 0, w, h, s_game, NULL, wc.hInstance, NULL);
    if (!s_menu)
        fprintf(stderr, "[VIDEO] could not open the Video window (%lu)\n", GetLastError());
}

static void show_menu(void)
{
    ensure_menu();
    if (!s_menu)
        return;
    seed_session();
    s_committed = s_session;
    s_draft = s_session;
    s_row = 0;
    s_open = 1;
    snprintf(s_status, sizeof s_status, "Preview with Left and Right. Nothing is saved yet.");
    place_menu();
    ShowWindow(s_menu, SW_SHOW);
    SetFocus(s_menu);
    InvalidateRect(s_menu, NULL, FALSE);
}

static void toggle_menu(void)
{
    if (s_open)
        hide_menu(1);
    else
        show_menu();
}

static int menu_wants_message(UINT m)
{
    return m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_KEYUP || m == WM_SYSKEYUP
        || m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN
        || m == WM_XBUTTONDOWN || m == WM_LBUTTONUP || m == WM_RBUTTONUP
        || m == WM_MBUTTONUP || m == WM_XBUTTONUP
        || m == WM_MOUSEWHEEL || m == WM_INPUT || m == WM_SETFOCUS;
}

static LRESULT CALLBACK game_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && w == VK_F10 && (l & (1L << 30)) == 0) {
        toggle_menu();
        return 0;
    }
    if (s_open && menu_wants_message(m)) {
        if (s_menu)
            SetFocus(s_menu);
        return (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP || m == WM_MOUSEWHEEL) ? TRUE : 0;
    }
    if (s_old)
        return CallWindowProcA(s_old, h, m, w, l);
    return DefWindowProcA(h, m, w, l);
}

/* LONG_PTR and WNDPROC are the same width. Copying the bits avoids a
 * pointer/integer cast mingw-clang warns on under -Werror. */
_Static_assert(sizeof(WNDPROC) == sizeof(LONG_PTR), "WNDPROC width");

static LONG_PTR proc_to_long(WNDPROC fn)
{
    LONG_PTR v = 0;
    memcpy(&v, &fn, sizeof fn);
    return v;
}

static WNDPROC proc_from_long(LONG_PTR v)
{
    WNDPROC fn = NULL;
    memcpy(&fn, &v, sizeof fn);
    return fn;
}

static void attach_game(HWND h)
{
    LONG_PTR prev;
    if (s_game)
        return;
    s_game = h;
    prev = SetWindowLongPtrA(h, GWLP_WNDPROC, proc_to_long(game_proc));
    s_old = proc_from_long(prev);
    fprintf(stderr, "[VIDEO] F10 opens Video options\n");
}

static DWORD WINAPI wait_for_game(LPVOID unused)
{
    (void)unused;
    for (;;) {
        HWND h = FindWindowA(GAME_CLASS, NULL);
        if (h) {
            attach_game(h);
            return 0;
        }
        Sleep(200);
    }
}

void video_menu_start(void)
{
    HANDLE thread;
    if (s_started)
        return;
    s_started = 1;
    s_env_override = any_env_override();
    s_place.length = sizeof s_place;
    fprintf(stderr, "[VIDEO] F10 opens Video options when the D3D11 window (%s) exists\n",
            GAME_CLASS);
    thread = CreateThread(NULL, 0, wait_for_game, NULL, 0, NULL);
    if (thread)
        CloseHandle(thread);
    else
        fprintf(stderr, "[VIDEO] could not watch for the D3D11 window; F10 will not open Video\n");
}
