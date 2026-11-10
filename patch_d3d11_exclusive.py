"""patch_d3d11_exclusive.py - idempotent. Exclusive fullscreen on D3D11.

Pin 0692070's present.fullscreen is a borderless popup (any non-zero value).
This patch keeps 0 as windowed and 1 as borderless, and treats 2 as
exclusive: IDXGISwapChain::SetFullscreenState(TRUE, NULL) on the desktop mode
of the output the window is on. The 4:3 frame stays letterboxed in that
client (the scaling blit already handles any client size).

Flip-driven state machine (all on the window thread, which pumps messages):
  - wnd_proc only RECORDS focus (WM_ACTIVATEAPP) and the new client size
    (WM_SIZE, already there). It never calls DXGI from inside a message.
  - exclusive_service(), at the top of every flip and before present_resize:
      * reads the truth from GetFullscreenState (DXGI itself leaves exclusive
        on Alt+Tab, so a private flag would go stale);
      * wanted (opts.fullscreen >= 2), focused, not minimised, not exclusive:
        SetFullscreenState(TRUE). DXGI_ERROR_NOT_CURRENTLY_AVAILABLE (no
        focus yet) is retried every 30 flips, 20 times, and again at once
        when the window gains focus or the mode is asked for again;
      * exclusive but not wanted, or the app lost focus: SetFullscreenState
        (FALSE).
  - SetFullscreenState sends WM_SIZE with the new client. present_resize()
    (next lines of the same flip) then ResizeBuffers to it. ResizeBuffers is
    legal while exclusive; on a flip-model chain it must repeat the creation
    flags, so ALLOW_MODE_SWITCH is passed there too.
  - WM_CLOSE leaves exclusive before ExitProcess so the mode is restored.
  - Alt+Enter is suppressed (MWA_NO_ALT_ENTER): the PC options page owns it.

Exports (no prototype in the toolkit): breakdown_display_set_exclusive(int)
re-arms the retry counters; breakdown_display_exclusive_active() says whether
DXGI exclusive is on. src/display_host.c tests the first one's address.

Run after patch_d3d11_window.py. Refuses if an anchor moved. Idempotent via
MARK. Set BREAKDOWN_D3D11_FILE to patch another path (tests).
"""
import io
import os
import pathlib

P = pathlib.Path(os.environ.get("BREAKDOWN_D3D11_FILE")
                 or pathlib.Path("external") / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c")
MARK = "[disp-exclusive]"


STATE = """/* {mark} exclusive fullscreen state. See patch_d3d11_exclusive.py. */
static int s_ex_active = 1;     /* WM_ACTIVATEAPP: the app has focus */
static int s_ex_on;             /* last GetFullscreenState / our own call */
static int s_ex_prev_want;
static unsigned s_ex_tries, s_ex_cool;
static void exclusive_leave(void);

"""

FUNCS = """/* {mark} 0 windowed, 1 borderless, 2 exclusive (desktop mode of the output). */
#define EX_COOLDOWN_FLIPS 30u
#define EX_MAX_TRIES 20u

static void exclusive_bind(void)
{{
    IDXGIFactory *fac = NULL;
    if (!s_swap || !s_hwnd)
        return;
    if (SUCCEEDED(IDXGISwapChain_GetParent(s_swap, &IID_IDXGIFactory, (void **)&fac)) && fac) {{
        IDXGIFactory_MakeWindowAssociation(fac, s_hwnd, DXGI_MWA_NO_ALT_ENTER);
        IDXGIFactory_Release(fac);
    }}
}}

static void exclusive_leave(void)
{{
    BOOL fs = FALSE;
    HRESULT hr;
    if (!s_swap)
        return;
    if (FAILED(IDXGISwapChain_GetFullscreenState(s_swap, &fs, NULL)))
        fs = FALSE;
    if (!fs) {{
        s_ex_on = 0;
        return;
    }}
    hr = IDXGISwapChain_SetFullscreenState(s_swap, FALSE, NULL);
    s_ex_on = 0;
    if (FAILED(hr))
        fprintf(stderr, LOGP "exclusive fullscreen off hr=0x%08lX\\n", (unsigned long)hr);
    else
        fprintf(stderr, LOGP "exclusive fullscreen off\\n");
    /* One more WM_WINDOWPOSCHANGED after the flag is clear, so the game's
     * window code can finish a restyle it held back while exclusive. */
    if (s_hwnd && IsWindow(s_hwnd))
        SetWindowPos(s_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}}

/* The game asked for exclusive again (or focus came back): retry now. */
void breakdown_display_set_exclusive(int on)
{{
    (void)on;
    s_ex_tries = 0;
    s_ex_cool = 0;
}}

int breakdown_display_exclusive_active(void)
{{
    return s_ex_on;
}}

/* Window thread, at the top of d3d11_flip, before present_resize. */
static void exclusive_service(void)
{{
    int want;
    BOOL fs = FALSE;
    HRESULT hr;

    if (!s_swap || !s_hwnd)
        return;
    want = nv2a_host_opts()->fullscreen >= 2;
    if (want && !s_ex_prev_want) {{
        s_ex_tries = 0;
        s_ex_cool = 0;
    }}
    s_ex_prev_want = want;
    if (FAILED(IDXGISwapChain_GetFullscreenState(s_swap, &fs, NULL)))
        fs = FALSE;
    if (s_ex_on && !fs)
        fprintf(stderr, LOGP "exclusive fullscreen ended by DXGI (focus lost or Alt+Tab)\\n");
    s_ex_on = fs ? 1 : 0;
    if (fs) {{
        if (!want || !s_ex_active)
            exclusive_leave();
        return;
    }}
    if (!want || !s_ex_active || IsIconic(s_hwnd))
        return;
    if (s_ex_cool) {{
        s_ex_cool--;
        return;
    }}
    if (s_ex_tries >= EX_MAX_TRIES)
        return;
    exclusive_bind();
    hr = IDXGISwapChain_SetFullscreenState(s_swap, TRUE, NULL);
    if (FAILED(hr)) {{
        s_ex_tries++;
        s_ex_cool = EX_COOLDOWN_FLIPS;
        if (s_ex_tries == 1 || s_ex_tries == EX_MAX_TRIES)
            fprintf(stderr, LOGP "exclusive fullscreen failed hr=0x%08lX (try %u of %u,"
                    " retried on focus; the window stays as it is)\\n",
                    (unsigned long)hr, s_ex_tries, EX_MAX_TRIES);
        return;
    }}
    s_ex_tries = 0;
    s_ex_on = 1;
    fprintf(stderr, LOGP "exclusive fullscreen on\\n");
}}

""".format(mark=MARK)


def sub(s, old, new, what):
    if s.count(old) != 1:
        raise SystemExit("toolkit changed - %s was not found exactly once" % what)
    return s.replace(old, new, 1)


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s and "void breakdown_display_set_exclusive" in s:
        print("already patched")
        return
    if MARK in s or "breakdown_display_set_exclusive" in s:
        raise SystemExit("partial or older exclusive patch - restore nv2a_pb_d3d11.c and re-run")
    nl = "\r\n" if "\r\n" in s else "\n"

    def n(text):
        return text.replace("\n", nl)

    # 1. state and a forward declaration, before wnd_proc.
    wnd = "static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)"
    s = sub(s, wnd, n(STATE.format(mark=MARK)) + wnd, "wnd_proc")

    # 2. WM_CLOSE leaves exclusive so the desktop mode comes back.
    close = "    if (m == WM_CLOSE) {" + nl
    s = sub(s, close, close
            + "        exclusive_leave(); /* " + MARK + " restore the desktop mode */" + nl,
            "WM_CLOSE handler")

    # 3. focus is recorded, never acted on inside the message.
    size = "    if (m == WM_SIZE && w != SIZE_MINIMIZED && LOWORD(l) && HIWORD(l)) {"
    activate = n(
        "    if (m == WM_ACTIVATEAPP) {\n"
        "        /* " + MARK + " Recorded only; the flip enters or leaves exclusive.\n"
        "         * ACTIVATEAPP, not ACTIVATE: an owned window (PC options) taking\n"
        "         * focus must not drop exclusive. */\n"
        "        s_ex_active = w != 0;\n"
        "        s_ex_tries = 0;\n"
        "        s_ex_cool = 0;\n"
        "    }\n")
    s = sub(s, size, activate + size, "WM_SIZE handler")

    # 4. the functions, before init().
    init = "static int init(void)"
    s = sub(s, init, n(FUNCS) + init, "init()")

    # 5. exclusive creates a normal window at the hook's client size; only 1
    #    is the monitor-sized popup.
    size_if = ("    if (full) {" + nl
               + "        /* present.fullscreen: borderless over the primary monitor (no HWND" + nl)
    s = sub(s, size_if, size_if.replace("if (full) {", "if (full == 1) { /* " + MARK + " 2 is exclusive */", 1),
            "borderless size block")
    win_if = ("    if (full) {" + nl
              + '        s_hwnd = CreateWindowW(L"" D3D11_WND_CLASS, title,' + nl)
    s = sub(s, win_if, win_if.replace("if (full) {", "if (full == 1) {", 1), "borderless window create")

    # 6. the chain may switch mode.
    swap = "    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;" + nl
    s = sub(s, swap, swap + "    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; /* " + MARK + " */" + nl,
            "swap chain setup")

    # 7. no Alt+Enter from DXGI.
    made = "    s_back_w = s_made_w = bw;" + nl + "    s_back_h = s_made_h = bh;" + nl
    s = sub(s, made, made + "    exclusive_bind(); /* " + MARK + " */" + nl, "swap chain size store")

    # 8. ResizeBuffers repeats the creation flags (flip model requirement).
    rb = "IDXGISwapChain_ResizeBuffers(s_swap, 0, cw, ch, DXGI_FORMAT_UNKNOWN, 0);"
    s = sub(s, rb, "IDXGISwapChain_ResizeBuffers(s_swap, 0, cw, ch, DXGI_FORMAT_UNKNOWN," + nl
            + "                                    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH); /* " + MARK + " */",
            "ResizeBuffers call")

    # 9. the service runs before present_resize so the new client is used at once.
    flip = "    present_resize();" + nl + "    s_drawn = present_target(surface_offset);" + nl
    s = sub(s, flip, "    exclusive_service(); /* " + MARK + " */" + nl + flip, "d3d11_flip present_resize call")

    for need in ("void breakdown_display_set_exclusive", "int breakdown_display_exclusive_active",
                 "static void exclusive_service", "if (full == 1) {"):
        if need not in s:
            raise SystemExit("internal error: missing " + need)
    if s.count("if (full == 1)") != 2:
        raise SystemExit("internal error: borderless tests")
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("patched d3d11 exclusive fullscreen")


if __name__ == "__main__":
    main()
