/*
 * pc_options_host.c - owned borderless overlay on the 4:3 picture.
 *
 * A swap-chain sprite would have to be drawn by the toolkit present path
 * (external/xboxrecomp, not in this repo). Present also covers a child HWND.
 * The page is an owned borderless window parked on the 4:3 picture instead.
 * Colors are procedural, not sampled from the game.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pc_options_host.h"
#include "video_config.h"
#include "a11y_input.h"
#include "skins.h"
#include "display_host.h"
#include "kernel/nv2a_backend_common.h"

#include <stdio.h>
#include <string.h>

#define GAME_CLASS "RecompNv2aD3D11"
#define PAGE_CLASS "BreakdownPcOptions"
#define PC_MSG (WM_APP + 0x57)

static const COLORREF COL_BG = RGB(10, 16, 28);
static const COLORREF COL_ROW = RGB(36, 64, 96);
static const COLORREF COL_TEXT = RGB(236, 240, 244);
static const COLORREF COL_DIM = RGB(168, 178, 190);
static const COLORREF COL_VALUE = RGB(232, 196, 120);

static HWND s_game;
static HWND s_page;
static WNDPROC s_old;
static HFONT s_font;
static HFONT s_title_font;
static CRITICAL_SECTION s_lock;
static volatile LONG s_lock_state;
static int s_started;
static int s_have_place;
static WINDOWPLACEMENT s_place;
static char b_scale[8];
static char b_window[8];
static char b_aspect[8];
static char b_filter[12];
static char b_full[12]; /* "0", "1", "exclusive" */
static char b_fps60[2], b_pipe[2], b_vsync[2];
static char b_repeat[8], b_toggle[8], b_outfit[12], b_text[8];   /* a11y_input.h tokens: "abxy", "both", ... */

struct pc_apply {
    int action;
    int restyle;
    struct video_settings settings;
};

static struct pc_apply s_q[16];
static int s_qr;
static int s_qw;
static int s_q_count;

static void ensure_lock(void)
{
    LONG state = InterlockedCompareExchange(&s_lock_state, 1, 0);
    if (state == 0) {
        InitializeCriticalSection(&s_lock);
        InterlockedExchange(&s_lock_state, 2);
        return;
    }
    while (InterlockedCompareExchange(&s_lock_state, 2, 2) != 2)
        Sleep(0);
}

static void lock(void)
{
    ensure_lock();
    EnterCriticalSection(&s_lock);
}

static void unlock(void)
{
    LeaveCriticalSection(&s_lock);
}

/* src/recomp_manual.c: switch 60 FPS gameplay on or off now. */
extern void breakdown_fps60_set(int on);
/* patch_d3d11_vsync.py: Present's sync interval, live. Weak: an unpatched toolkit leaves it NULL and VSync then
 * changes on the next launch (RECOMP_PRESENT_VSYNC is read at startup). */
extern void nv2a_d3d11_set_vsync(int on) __attribute__((weak));
/* src/recomp_manual.c: hold to repeat / hold toggle (A11Y-2/3), A11Y_REPEAT_* / A11Y_TOGGLE_*, live. */
extern void breakdown_a11y_set(int repeat_choice, int toggle_choice);
/* src/recomp_manual.c: hero outfit (S7.2), a skins.h choice; used from the next spawn. */
extern void breakdown_hero_outfit_set(int choice);

static unsigned env_locks(void)
{
    unsigned mask = 0;
    const char *v;
    v = getenv("RECOMP_RENDER_SCALE");
    if (v && v[0]) mask |= PC_LOCK_SCALE;
    v = getenv("RECOMP_WINDOW_SCALE");
    if (v && v[0]) mask |= PC_LOCK_WINDOW;
    v = getenv("RECOMP_DISPLAY_ASPECT");
    if (v && v[0]) mask |= PC_LOCK_ASPECT;
    v = getenv("RECOMP_PRESENT_FILTER");
    if (v && v[0]) mask |= PC_LOCK_FILTER;
    v = getenv("RECOMP_PRESENT_FULLSCREEN");
    if (v && v[0]) mask |= PC_LOCK_MODE;
    /* These two are copied from the file into the environment at startup; lock only launch values. */
    if (display_host_launch_set("RECOMP_FPS60")) mask |= PC_LOCK_FPS60;
    if (display_host_launch_set("RECOMP_PB_PIPELINE")) mask |= PC_LOCK_PIPE;
    if (display_host_launch_set("RECOMP_PRESENT_VSYNC")) mask |= PC_LOCK_VSYNC;
    if (display_host_launch_set("RECOMP_A11Y_REPEAT")) mask |= PC_LOCK_REPEAT;
    if (display_host_launch_set("RECOMP_A11Y_TOGGLE")) mask |= PC_LOCK_TOGGLE;
    if (display_host_launch_set("RECOMP_HERO_OUTFIT")) mask |= PC_LOCK_OUTFIT;
    if (display_host_launch_set("RECOMP_PC_TEXT_SIZE")) mask |= PC_LOCK_TEXT;
    return mask;
}

static void refresh_baseline(void)
{
    struct display_choice c;
    struct video_settings s;
    if (pc_options_is_open())
        return;
    display_host_choose(display_getenv_layered, &c);
    video_settings_from_choice(&c, &s);
    pc_options_set_baseline(&s);
    pc_options_set_locks(env_locks());
}

static int s_save_inline;
static struct video_settings s_save_inline_settings;

/* 1 when the game window will run the action. 0 when there is no window yet. */
static int enqueue(int action, int restyle, const struct video_settings *s)
{
    struct pc_apply item;
    if (!s_game || !IsWindow(s_game))
        return 0;
    item.action = action;
    item.restyle = restyle;
    item.settings = *s;
    if (s_q_count == 16) {
        s_qr = (s_qr + 1) % 16;
        s_q_count--;
    }
    s_q[s_qw] = item;
    s_qw = (s_qw + 1) % 16;
    s_q_count++;
    PostMessageA(s_game, PC_MSG, 0, 0);
    return 1;
}

static int dequeue(struct pc_apply *out)
{
    if (s_q_count == 0)
        return 0;
    *out = s_q[s_qr];
    s_qr = (s_qr + 1) % 16;
    s_q_count--;
    return 1;
}

static char *bound_get(const char *name)
{
    if (strcmp(name, "RECOMP_RENDER_SCALE") == 0) return b_scale;
    if (strcmp(name, "RECOMP_WINDOW_SCALE") == 0) return b_window;
    if (strcmp(name, "RECOMP_DISPLAY_ASPECT") == 0) return b_aspect;
    if (strcmp(name, "RECOMP_PRESENT_FILTER") == 0) return b_filter;
    if (strcmp(name, "RECOMP_PRESENT_FULLSCREEN") == 0) return b_full;
    if (strcmp(name, "RECOMP_FPS60") == 0) return b_fps60;
    if (strcmp(name, "RECOMP_PB_PIPELINE") == 0) return b_pipe;
    if (strcmp(name, "RECOMP_PRESENT_VSYNC") == 0) return b_vsync;
    if (strcmp(name, "RECOMP_A11Y_REPEAT") == 0) return b_repeat;
    if (strcmp(name, "RECOMP_A11Y_TOGGLE") == 0) return b_toggle;
    if (strcmp(name, "RECOMP_HERO_OUTFIT") == 0) return b_outfit;
    if (strcmp(name, "RECOMP_PC_TEXT_SIZE") == 0) return b_text;
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
    snprintf(b_full, sizeof b_full, "%s", video_settings_fullscreen_token(c.borderless));
    snprintf(b_fps60, sizeof b_fps60, "%d", c.fps60 ? 1 : 0);
    snprintf(b_pipe, sizeof b_pipe, "%d", c.pipeline ? 1 : 0);
    snprintf(b_vsync, sizeof b_vsync, "%d", c.vsync ? 1 : 0);
    snprintf(b_repeat, sizeof b_repeat, "%s", a11y_repeat_token(c.a11y_repeat));
    snprintf(b_toggle, sizeof b_toggle, "%s", a11y_toggle_token(c.a11y_toggle));
    snprintf(b_outfit, sizeof b_outfit, "%s", skins_choice_token(c.outfit));
    snprintf(b_text, sizeof b_text, "%d", video_text_size_percent(c.text_size));
}

static int style_is_borderless(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    return (style & WS_POPUP) && !(style & WS_CAPTION);
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

static void place_page(void);

/* Window thread only. Set by the D3D11 exclusive patch; NULL on a toolkit
 * without it. */
static int exclusive_active(void)
{
#if defined(__GNUC__) || defined(__clang__)
    if (!&breakdown_display_exclusive_active)
        return 0;
#endif
    return breakdown_display_exclusive_active();
}

/* A restyle asked for while DXGI owns the window. DXGI puts the old windowed
 * size back when the flip leaves exclusive; this runs after that. */
static struct display_choice s_held;
static int s_held_valid;

static void resize_game(const struct display_choice *c)
{
    DWORD style;
    RECT frame;
    int was_borderless, x, y, w, h;

    if (!s_game || !IsWindow(s_game) || !c)
        return;
    if (exclusive_active()) {
        /* Moving the window under DXGI fights the mode switch. Staying in
         * exclusive needs nothing; anything else waits for the flip to leave. */
        if (c->fullscreen != DISPLAY_EXCLUSIVE) {
            s_held = *c;
            s_held_valid = 1;
        }
        place_page();
        return;
    }
    s_held_valid = 0;
    was_borderless = style_is_borderless(s_game);
    /* Exclusive is entered from the windowed client size (the path below):
     * the D3D11 patch calls SetFullscreenState on the next flip and DXGI
     * puts this size back when it leaves. */
    if (c->fullscreen == DISPLAY_BORDERLESS) {
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
        place_page();
        return;
    }
    style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    frame.left = 0;
    frame.top = 0;
    frame.right = (LONG)c->window_w;
    frame.bottom = (LONG)c->window_h;
    /* Same DPI space as the rest of this process. Do not turn on
     * per-monitor awareness here; that would resize the whole game. */
    AdjustWindowRect(&frame, style, FALSE);
    w = frame.right - frame.left;
    h = frame.bottom - frame.top;
    windowed_origin(was_borderless, w, h, &x, &y);
    SetWindowLongPtrA(s_game, GWL_STYLE, (LONG_PTR)style);
    SetWindowPos(s_game, NULL, x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
    place_page();
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
            "[PC] apply render.scale=%u filter=%s fullscreen=%d (0 win, 1 borderless, 2 exclusive) window=%ux%u aspect=%s\n",
            c->render_scale, c->filter_name, c->fullscreen, c->window_w, c->window_h, c->aspect);
}

static int apply_live(const struct video_settings *s, int restyle)
{
    struct display_choice c;
    int have = s_game && IsWindow(s_game);
    bind_settings(s);
    display_host_choose(bound_get, &c);
    request_opts(&c);
    breakdown_fps60_set(s->fps60);   /* 60 FPS switches at once; the renderer setting waits for a restart */
    breakdown_a11y_set(s->a11y_repeat, s->a11y_toggle);
    breakdown_hero_outfit_set(s->outfit);
    if (nv2a_d3d11_set_vsync) {
        static int s_vs = -1;
        nv2a_d3d11_set_vsync(s->vsync);
        if (s_vs != (s->vsync ? 1 : 0)) {
            s_vs = s->vsync ? 1 : 0;
            fprintf(stderr, "[PC] VSync %s (live)\n", s_vs ? "on" : "off");
            fflush(stderr);
        }
    }
    if (restyle && have)
        resize_game(&c);
    else if (!have)
        fprintf(stderr, "[PC] no D3D11 window yet. Size and borderless apply on the next launch.\n");
    return have;
}

static int s_placing;

static void place_page(void)
{
    RECT client;
    struct pc_rect panel;
    POINT origin;
    if (s_placing)
        return;
    if (!s_page || !s_game || !IsWindow(s_page) || !IsWindow(s_game))
        return;
    if (!GetClientRect(s_game, &client))
        return;
    pc_options_panel_rect((unsigned)(client.right - client.left),
                          (unsigned)(client.bottom - client.top), &panel);
    if (panel.w < 1 || panel.h < 1)
        return;
    /* Screen coordinates. A child HWND is covered by the swap-chain Present. */
    origin.x = panel.x;
    origin.y = panel.y;
    if (!ClientToScreen(s_game, &origin))
        return;
    /* Owned popups already stay above their owner. HWND_TOP would raise
     * that whole group above every other application on each move.
     * Client and screen points stay in this process's DPI space. The exe
     * does not call SetProcessDpiAwareness, so these match the game window. */
    s_placing = 1;
    SetWindowPos(s_page, NULL, origin.x, origin.y, panel.w, panel.h,
                 SWP_NOZORDER | SWP_NOACTIVATE |
                 (IsWindowVisible(s_page) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    s_placing = 0;
}

static LRESULT CALLBACK page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK game_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void ensure_class_and_page(void)
{
    WNDCLASSA wc;
    if (s_page || !s_game)
        return;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = page_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = PAGE_CLASS;
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
    /* Owned popup, no caption and no taskbar button. Present covers a child. */
    s_page = CreateWindowExA(WS_EX_TOOLWINDOW, PAGE_CLASS, "PC options", WS_POPUP,
                             0, 0, 10, 10, s_game, NULL, wc.hInstance, NULL);
    if (!s_page)
        fprintf(stderr, "[PC] could not create the host overlay (%lu)\n", GetLastError());
}

static void show_page(void)
{
    ensure_class_and_page();
    if (!s_page)
        return;
    place_page();
    ShowWindow(s_page, SW_SHOW);
    SetFocus(s_page);
    ClipCursor(NULL);
    InvalidateRect(s_page, NULL, FALSE);
}

static void hide_page(void)
{
    if (s_page)
        ShowWindow(s_page, SW_HIDE);
    if (s_game && IsWindow(s_game))
        SetFocus(s_game);
}

static void save_snapshot(const struct video_settings *s)
{
    char path[512];
    int ok = 0;
    if (video_config_beside_exe(path, sizeof path) == 0 && video_config_save(path, s) == 0) {
        ok = 1;
        fprintf(stderr, "[PC] saved %s\n", path);
    } else {
        fprintf(stderr, "[PC] could not save the video config\n");
    }
    lock();
    pc_options_save_result(ok);
    unlock();
    if (s_page)
        InvalidateRect(s_page, NULL, FALSE);
}

static void perform(const struct pc_apply *item)
{
    if (item->action == PC_ACT_OPENED) {
        show_page();
        return;
    }
    if (item->action == PC_ACT_SAVE) {
        save_snapshot(&item->settings);
        return;
    }
    if (item->action == PC_ACT_PREVIEW) {
        apply_live(&item->settings, item->restyle);
        place_page();
        if (s_page)
            InvalidateRect(s_page, NULL, FALSE);
        return;
    }
    if (item->action == PC_ACT_REVERT) {
        apply_live(&item->settings, 1);
        hide_page();
    }
}

static unsigned bit_for_vk(WPARAM vk, int open)
{
    if (vk == VK_UP) return PC_PAD_DPAD_UP;
    if (vk == VK_DOWN) return PC_PAD_DPAD_DOWN;
    if (vk == VK_LEFT) return PC_PAD_DPAD_LEFT;
    if (vk == VK_RIGHT) return PC_PAD_DPAD_RIGHT;
    if (vk == VK_BACK) return PC_PAD_BACK;
    if (vk == VK_RETURN) return open ? PC_PAD_A : PC_PAD_START;
    return 0;
}

/* What the page last drew. The game polls the pad every frame, so a "nothing happened" poll used to
 * redraw the whole page ~60 times a second, straight to the screen: the overlay flashed. Now such a poll
 * redraws only when the row or the status line moved; value changes come through PC_ACT_PREVIEW. */
static int s_painted_row = -1;
static char s_painted_status[160];

static void note_result(const struct pc_pad_result *r, int restyle)
{
    const struct video_settings *src;
    if (!r || r->action == PC_ACT_NONE) {
        int changed;
        lock();
        changed = pc_options_row() != s_painted_row
                  || strncmp(pc_options_status(), s_painted_status, sizeof s_painted_status - 1) != 0;
        unlock();
        if (changed && s_page)
            InvalidateRect(s_page, NULL, FALSE);
        return;
    }
    src = (r->action == PC_ACT_REVERT) ? pc_options_committed() : pc_options_draft();
    if (enqueue(r->action, restyle, src))
        return;
    /* Apply still has to hit the file when the D3D window is not up yet. */
    if (r->action == PC_ACT_SAVE) {
        s_save_inline = 1;
        s_save_inline_settings = *src;
    }
}

static void flush_inline_save(void)
{
    struct video_settings s;
    int do_it = 0;
    lock();
    if (s_save_inline) {
        s = s_save_inline_settings;
        s_save_inline = 0;
        do_it = 1;
    }
    unlock();
    if (do_it)
        save_snapshot(&s);
}

static void on_key(WPARAM vk, int down)
{
    unsigned bit;
    struct pc_pad_result r;
    int restyle = 0;
    int open;
    if (vk == VK_F10 && down) {
        lock();
        refresh_baseline();
        r = pc_options_on_f10();
        restyle = (r.action == PC_ACT_REVERT);
        note_result(&r, restyle);
        unlock();
        flush_inline_save();
        return;
    }
    if (vk == VK_ESCAPE && down) {
        lock();
        r = pc_options_on_esc();
        note_result(&r, r.action == PC_ACT_REVERT);
        unlock();
        flush_inline_save();
        return;
    }
    if (!down && vk == VK_RETURN) {
        lock();
        pc_options_set_ui_bit(PC_PAD_A, 0);
        r = pc_options_set_ui_bit(PC_PAD_START, 0);
        note_result(&r, 0);
        unlock();
        flush_inline_save();
        return;
    }
    lock();
    open = pc_options_is_open();
    bit = bit_for_vk(vk, open);
    if (!bit) {
        unlock();
        return;
    }
    if (!open)
        refresh_baseline();
    r = pc_options_set_ui_bit(bit, down);
    if (r.action == PC_ACT_PREVIEW)
        restyle = (vk == VK_LEFT || vk == VK_RIGHT || pc_options_row() == PC_ROW_RESET);
    if (r.action == PC_ACT_REVERT)
        restyle = 1;
    note_result(&r, restyle);
    unlock();
    flush_inline_save();
}

/* A11Y-4: page text grows with the page height (16 px at 800 px tall) times the Text size row (100/150/200%).
 * Fonts are re-made only when that size changes. */
static HFONT s_font_sized, s_title_sized;
static int s_font_px;
static int s_first_row;   /* first row shown when they do not all fit (text size 150/200%) */
static void page_fonts(int px)
{
    HFONT f, t;
    if (px == s_font_px && s_font_sized)
        return;
    f = CreateFontA(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    t = CreateFontA(-(px * 22 / 16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    if (!f || !t) {
        if (f)
            DeleteObject(f);
        if (t)
            DeleteObject(t);
        return;   /* keep the last good pair (or the 16/22 px fonts) */
    }
    if (s_font_sized)
        DeleteObject(s_font_sized);
    if (s_title_sized)
        DeleteObject(s_title_sized);
    s_font_sized = f;
    s_title_sized = t;
    s_font_px = px;
}

static void paint_page(HDC hdc, const RECT *rc)
{
    char titles[PC_ROW_COUNT][48];
    char values[PC_ROW_COUNT][48];
    char status[160];
    char detail[240];
    char more[64];
    int row, open, i, y, row_step, text_pct, rows_top, rows_bottom, shown, first, footer, line_h;
    float k, hk;
    RECT line;
    HBRUSH bg, hi;
    HFONT font, title_font;

    lock();
    open = pc_options_is_open();
    row = pc_options_row();
    text_pct = video_text_size_percent(pc_options_draft()->text_size);   /* the draft: it previews as it changes */
    snprintf(status, sizeof status, "%s", pc_options_status());
    s_painted_row = row;
    snprintf(s_painted_status, sizeof s_painted_status, "%s", status);
    for (i = 0; i < PC_ROW_COUNT; i++) {
        snprintf(titles[i], sizeof titles[i], "%s", pc_options_row_title(i));
        pc_options_row_value(i, values[i], sizeof values[i]);
    }
    if (row <= PC_ROW_LAST_SETTING)
        snprintf(detail, sizeof detail, "%s", video_row_detail(pc_options_draft(), row));
    else if (row == PC_ROW_APPLY)
        snprintf(detail, sizeof detail, "%s", "Write breakdown_video.cfg beside the exe. This page stays open.");
    else if (row == PC_ROW_RESET)
        snprintf(detail, sizeof detail, "%s", "Preview stock settings. Locked rows stay on the launch script.");
    else
        snprintf(detail, sizeof detail, "%s", "Put the picture back and close. The file stays as it was.");
    unlock();
    if (!open)
        return;

    hk = (float)rc->bottom / 800.0f;
    if (hk < 0.75f)
        hk = 0.75f;
    if (hk > 2.0f)
        hk = 2.0f;
    k = hk * (float)text_pct / 100.0f;
    page_fonts((int)(16.0f * k + 0.5f));
    font = s_font_sized ? s_font_sized : s_font;
    title_font = s_title_sized ? s_title_sized : (s_title_font ? s_title_font : s_font);
    line_h = (int)(22.0f * k + 0.5f);

    bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, rc, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, title_font);
    SetTextColor(hdc, COL_TEXT);
    line.left = (int)(16 * k);
    line.top = (int)(10 * k);
    line.right = rc->right - (int)(16 * k);
    line.bottom = (int)(40 * k);
    DrawTextA(hdc, "PC options", -1, &line, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, font);
    SetTextColor(hdc, COL_DIM);
    line.top = line.bottom;
    line.bottom = line.top + line_h;
    /* (the subtitle is drawn below, once we know whether a "more above/below" note shares its line) */

    /* Footer: the help text (up to two lines) and the status line. */
    footer = (int)(10 * k) + line_h + 4 + 2 * line_h + 4;
    rows_top = line.bottom + (int)(8 * k);
    rows_bottom = rc->bottom - footer;
    row_step = (int)(28.0f * k + 0.5f);
    if (row_step < line_h + 2)
        row_step = line_h + 2;
    shown = (rows_bottom - rows_top) / row_step;
    if (shown < 1)
        shown = 1;
    if (shown >= PC_ROW_COUNT) {
        shown = PC_ROW_COUNT;
        s_first_row = 0;
    } else {
        if (row < s_first_row)
            s_first_row = row;
        if (row >= s_first_row + shown)
            s_first_row = row - shown + 1;
        if (s_first_row > PC_ROW_COUNT - shown)
            s_first_row = PC_ROW_COUNT - shown;
        if (s_first_row < 0)
            s_first_row = 0;
    }
    first = s_first_row;
    more[0] = 0;
    if (shown < PC_ROW_COUNT) {
        /* Not every row fits: say so, on the subtitle line's right; the subtitle stops short of it. */
        SIZE ext;
        snprintf(more, sizeof more, "%s%s%s", first > 0 ? "more above" : "",
                 first > 0 && first + shown < PC_ROW_COUNT ? " / " : "",
                 first + shown < PC_ROW_COUNT ? "more below" : "");
        SetTextColor(hdc, COL_VALUE);
        DrawTextA(hdc, more, -1, &line, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        if (GetTextExtentPoint32A(hdc, more, (int)strlen(more), &ext))
            line.right -= ext.cx + (int)(24 * k);
    }
    SetTextColor(hdc, COL_DIM);
    DrawTextA(hdc, "Host overlay on the 4:3 picture. Not drawn into the frame.",
              -1, &line, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    hi = CreateSolidBrush(COL_ROW);
    for (i = first; i < first + shown; i++) {
        RECT rowrc;
        int valw = (int)(170 * k);
        y = rows_top + (i - first) * row_step;
        rowrc.left = (int)(10 * k);
        rowrc.top = y;
        rowrc.right = rc->right - (int)(10 * k);
        rowrc.bottom = y + row_step - 2;
        if (i == row)
            FillRect(hdc, &rowrc, hi);
        SetTextColor(hdc, i == row ? COL_TEXT : COL_DIM);
        line = rowrc;
        line.left = (int)(18 * k);
        line.right = rc->right - valw - (int)(10 * k);
        DrawTextA(hdc, titles[i], -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (values[i][0]) {
            SetTextColor(hdc, COL_VALUE);
            line.left = rc->right - valw;
            line.right = rc->right - (int)(16 * k);
            DrawTextA(hdc, values[i], -1, &line, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    DeleteObject(hi);
    SetTextColor(hdc, COL_TEXT);
    line.left = (int)(16 * k);
    line.right = rc->right - (int)(16 * k);
    line.bottom = rc->bottom - (int)(10 * k);
    line.top = line.bottom - line_h;
    DrawTextA(hdc, status, -1, &line, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SetTextColor(hdc, COL_DIM);
    line.bottom = line.top - 4;
    line.top = line.bottom - 2 * line_h;
    DrawTextA(hdc, detail, -1, &line, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HDC mem;
        HBITMAP bmp, old;
        GetClientRect(hwnd, &rc);
        /* Off-screen first, then one copy: no half-drawn page between the clear and the text. */
        mem = CreateCompatibleDC(hdc);
        bmp = mem ? CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top) : NULL;
        if (mem && bmp) {
            old = (HBITMAP)SelectObject(mem, bmp);
            paint_page(mem, &rc);
            BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
        } else {
            paint_page(hdc, &rc);
        }
        if (bmp)
            DeleteObject(bmp);
        if (mem)
            DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        /* F10 is handled on the game window so it is not toggled twice. */
        if (wparam == VK_F10)
            return 0;
        on_key(wparam, 1);
        return 0;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        on_key(wparam, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        HWND next = (HWND)wparam;
        if (next != s_game && next != s_page) {
            lock();
            pc_options_release_ui();
            unlock();
        }
    }
    if (msg == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static int page_key(WPARAM vk)
{
    return vk == VK_F10 || vk == VK_ESCAPE || vk == VK_UP || vk == VK_DOWN
        || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RETURN || vk == VK_BACK;
}

static LRESULT CALLBACK game_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == PC_MSG) {
        for (;;) {
            struct pc_apply item;
            int got;
            lock();
            got = dequeue(&item);
            unlock();
            if (!got)
                break;
            perform(&item);
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        s_page = NULL;
        s_game = NULL;
        if (s_old)
            return CallWindowProcA(s_old, hwnd, msg, wparam, lparam);
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_KILLFOCUS) {
        HWND next = (HWND)wparam;
        if (next != s_game && next != s_page) {
            lock();
            pc_options_release_ui();
            unlock();
        }
    }
    if ((msg == WM_SIZE || msg == WM_MOVE || msg == WM_WINDOWPOSCHANGED)
        && s_held_valid && !exclusive_active()) {
        struct display_choice held = s_held;
        s_held_valid = 0;
        resize_game(&held);
    }
    if (msg == WM_SIZE || msg == WM_MOVE || msg == WM_WINDOWPOSCHANGED)
        place_page();
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && page_key(wparam)) {
        int repeat = (lparam & (1L << 30)) != 0;
        int focused = s_page && GetFocus() == s_page;
        if (!(wparam == VK_F10 && repeat) && (wparam == VK_F10 || !focused))
            on_key(wparam, 1);
        if (wparam == VK_F10 || (s_page && IsWindowVisible(s_page)))
            return 0;
    }
    if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && page_key(wparam)) {
        if (!(s_page && GetFocus() == s_page))
            on_key(wparam, 0);
        if (wparam == VK_F10 || (s_page && IsWindowVisible(s_page)))
            return 0;
    }
    if (s_old)
        return CallWindowProcA(s_old, hwnd, msg, wparam, lparam);
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

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

static void attach_game(HWND hwnd)
{
    LONG_PTR prev;
    if (s_game)
        return;
    s_game = hwnd;
    prev = SetWindowLongPtrA(hwnd, GWLP_WNDPROC, proc_to_long(game_proc));
    s_old = proc_from_long(prev);
    fprintf(stderr, "[PC] Back+Start opens the host overlay over the 4:3 picture. F10 toggles it.\n");
}

static DWORD WINAPI wait_for_game(LPVOID unused)
{
    (void)unused;
    for (;;) {
        HWND hwnd = FindWindowA(GAME_CLASS, NULL);
        if (hwnd && hwnd != s_game && IsWindow(hwnd))
            attach_game(hwnd);
        Sleep(200);
    }
    return 0;
}

void pc_options_host_start(void)
{
    HANDLE thread;
    if (s_started)
        return;
    s_started = 1;
    ensure_lock();
    s_place.length = sizeof s_place;
    fprintf(stderr,
            "[PC] owned borderless host overlay over the 4:3 picture "
            "(not drawn into the frame, not the Xbox Options screen) "
            "when the D3D11 window (%s) exists. Back+Start opens it. F10 toggles it.\n",
            GAME_CLASS);
    thread = CreateThread(NULL, 0, wait_for_game, NULL, 0, NULL);
    if (thread)
        CloseHandle(thread);
    else
        fprintf(stderr, "[PC] could not watch for the D3D11 window\n");
}

struct pc_pad_result pc_options_host_on_port(int port, unsigned buttons)
{
    struct pc_pad_result r;
    int restyle;
    ensure_lock();
    lock();
    refresh_baseline();
    r = pc_options_on_port(port, buttons);
    restyle = 0;
    if (r.action == PC_ACT_PREVIEW)
        restyle = video_row_needs_window(pc_options_row()) || pc_options_row() == PC_ROW_RESET;
    if (r.action == PC_ACT_REVERT)
        restyle = 1;
    note_result(&r, restyle);
    unlock();
    flush_inline_save();
    return r;
}

/* The game's own Options list (VIDEO SETTINGS row, recomp_manual.c) opens this page. */
void pc_options_host_open(void)
{
    struct pc_pad_result r;
    ensure_lock();
    lock();
    if (pc_options_is_open()) {
        unlock();
        return;
    }
    refresh_baseline();
    r = pc_options_on_f10();
    note_result(&r, 0);
    unlock();
    flush_inline_save();
}
