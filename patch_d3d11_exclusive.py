"""patch_d3d11_exclusive.py - idempotent. Exclusive fullscreen on D3D11.

Pin 0692070's present.fullscreen is a borderless popup (any non-zero value).
Hor+ is not in that pin. This patch keeps 0 as windowed and 1 as borderless,
and treats 2 as exclusive: IDXGISwapChain::SetFullscreenState at the swap
chain's size (the window hook's client, not the monitor).

ResizeBuffers is refused while exclusive, so a resize leaves exclusive first.
The flip enters it again when nv2a_host_opts.fullscreen >= 2, after the
swap chain matches the client. Alt+Enter is suppressed so DXGI does not
switch mode behind the Video menu.

Run after patch_d3d11_window.py. Refuses if init() or the flip hook moved.
"""
import io
import pathlib

P = pathlib.Path("external") / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[disp-exclusive]"


FUNCS = """
/* {mark} 0 windowed, 1 borderless, 2 exclusive at the swap chain size.
 * The game calls breakdown_display_set_exclusive(0) before it restyles the
 * window. Entering waits until d3d11_on_flip, after present_resize. */
static int s_exclusive_on;
/* Set after SetFullscreenState fails, so the flip does not retry every frame.
 * Cleared when the game asks for windowed or borderless, and not set when
 * present_resize drops exclusive only long enough to ResizeBuffers. */
static int s_exclusive_hold;

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
    HRESULT hr;
    if (!s_swap || !s_exclusive_on)
        return;
    hr = IDXGISwapChain_SetFullscreenState(s_swap, FALSE, NULL);
    s_exclusive_on = 0;
    if (FAILED(hr))
        fprintf(stderr, LOGP "exclusive fullscreen off hr=0x%08lX\\n", (unsigned long)hr);
    else
        fprintf(stderr, LOGP "exclusive fullscreen off\\n");
}}

void breakdown_display_set_exclusive(int on)
{{
    HRESULT hr;

    if (!on) {{
        s_exclusive_hold = 0;
        exclusive_leave();
        return;
    }}
    if (!s_swap)
        return;
    exclusive_bind();
    hr = IDXGISwapChain_SetFullscreenState(s_swap, TRUE, NULL);
    if (FAILED(hr)) {{
        s_exclusive_on = 0;
        s_exclusive_hold = 1;
        fprintf(stderr, LOGP "exclusive fullscreen failed hr=0x%08lX"
                " (window stays at its client size)\\n", (unsigned long)hr);
        return;
    }}
    s_exclusive_hold = 0;
    s_exclusive_on = 1;
    fprintf(stderr, LOGP "exclusive fullscreen on\\n");
}}

""".format(mark=MARK)


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s and "void breakdown_display_set_exclusive" in s:
        print("already patched")
        return
    if MARK in s or "void breakdown_display_set_exclusive" in s:
        raise SystemExit("partial exclusive patch - restore nv2a_pb_d3d11.c and re-run")
    nl = "\r\n" if "\r\n" in s else "\n"
    funcs = FUNCS.replace("\n", nl)

    init = "static int init(void)"
    if s.count(init) != 1:
        raise SystemExit("toolkit changed - patch nv2a_pb_d3d11.c init() by hand")
    s = s.replace(init, funcs + init, 1)

    size_if = (
        "    if (full) {" + nl
        + "        /* present.fullscreen: borderless over the primary monitor (no HWND" + nl
        + "         * yet to ask MonitorFromWindow), no exclusive mode. */" + nl
    )
    size_if_new = (
        "    /* " + MARK + " 1 = borderless (monitor). 2 = exclusive, keeps bw/bh. */" + nl
        + "    if (full == 1) {" + nl
        + "        /* present.fullscreen: borderless over the primary monitor (no HWND" + nl
        + "         * yet to ask MonitorFromWindow). */" + nl
    )
    if s.count(size_if) != 1:
        raise SystemExit("toolkit changed - borderless size block was not found")
    s = s.replace(size_if, size_if_new, 1)

    swap = (
        "    sd.Windowed = TRUE;" + nl
        + "    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;" + nl
    )
    swap_new = (
        swap
        + "    /* " + MARK + " SetFullscreenState may change the output mode. */" + nl
        + "    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;" + nl
    )
    if s.count(swap) != 1:
        raise SystemExit("toolkit changed - swap chain windowed setup was not found")
    s = s.replace(swap, swap_new, 1)

    made = "    s_back_w = s_made_w = bw;" + nl + "    s_back_h = s_made_h = bh;" + nl
    made_new = made + "    exclusive_bind();" + nl
    if s.count(made) != 1:
        raise SystemExit("toolkit changed - swap chain size store was not found")
    s = s.replace(made, made_new, 1)

    done = "    s_failed = 0;" + nl + "    return 1;" + nl
    done_new = (
        "    if (full >= 2)" + nl
        + "        breakdown_display_set_exclusive(1);" + nl
        + done
    )
    if s.count(done) != 1:
        raise SystemExit("toolkit changed - init() success tail was not found")
    s = s.replace(done, done_new, 1)

    resize = (
        "    if (!cw || !ch || (cw == s_back_w && ch == s_back_h))" + nl
        + "        return;" + nl
        + "    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);" + nl
    )
    resize_new = (
        "    if (!cw || !ch || (cw == s_back_w && ch == s_back_h))" + nl
        + "        return;" + nl
        + "    /* " + MARK + " ResizeBuffers fails while exclusive. The flip enters again. */" + nl
        + "    if (s_exclusive_on)" + nl
        + "        exclusive_leave();" + nl
        + "    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);" + nl
    )
    if s.count(resize) != 1:
        raise SystemExit("toolkit changed - present_resize() was not found")
    s = s.replace(resize, resize_new, 1)

    flip = (
        "    changed = nv2a_host_opts_apply();" + nl
        + "    if ((changed & NV2A_OPT_SCALE) && s_dev)" + nl
        + "        scale_drop();" + nl
        + "}" + nl
    )
    flip_new = (
        "    changed = nv2a_host_opts_apply();" + nl
        + "    if ((changed & NV2A_OPT_SCALE) && s_dev)" + nl
        + "        scale_drop();" + nl
        + "    /* " + MARK + " fullscreen >= 2 is exclusive, after the swap chain matches. */" + nl
        + "    if (s_swap) {" + nl
        + "        int mode = nv2a_host_opts()->fullscreen;" + nl
        + "        if (mode >= 2 && !s_exclusive_on && !s_exclusive_hold)" + nl
        + "            breakdown_display_set_exclusive(1);" + nl
        + "        else if (mode < 2) {" + nl
        + "            s_exclusive_hold = 0;" + nl
        + "            if (s_exclusive_on)" + nl
        + "                exclusive_leave();" + nl
        + "        }" + nl
        + "    }" + nl
        + "}" + nl
    )
    if s.count(flip) != 1:
        raise SystemExit("toolkit changed - d3d11_on_flip() was not found")
    s = s.replace(flip, flip_new, 1)

    if s.count("void breakdown_display_set_exclusive") != 1:
        raise SystemExit("internal error: exclusive entry point missing")
    if s.count("if (full == 1)") != 1:
        raise SystemExit("internal error: borderless size test missing")
    if "no exclusive mode" in s:
        raise SystemExit("internal error: old exclusive comment still present")
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("patched d3d11 exclusive fullscreen")


if __name__ == "__main__":
    main()
