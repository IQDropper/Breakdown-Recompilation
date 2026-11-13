"""patch_d3d11_exclusive_why.py - idempotent. Say WHY exclusive fullscreen failed (run after patch_d3d11_exclusive.py).

DXGI refuses exclusive (SetFullscreenState -> 0x887A0004 DXGI_ERROR_UNSUPPORTED) when the window's monitor is wired to
a different GPU than the one the game renders on. Seen on a hybrid laptop (2026-11-12, tools in the GO kit): Intel UHD
630 drives the built-in screen, the GTX 1660 Ti drives the external one; a swap chain on either GPU goes exclusive on
its own monitor and fails with 0x887A0004 on the other (both flip and blt model).
On the first failure this prints, once: the monitor, the GPU wired to it, and the GPU the game renders on, plus what
to do. Nothing else changes (the retries and the window stay as they were). Anchors must each occur once."""
import io
import os
import pathlib

P = pathlib.Path(os.environ.get("BREAKDOWN_D3D11_FILE")
                 or pathlib.Path(__file__).resolve().parent / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c")
MARK = "[disp-exclusive-why]"

FUNC = r'''/* {mark} name the GPUs when DXGI refuses exclusive: the monitor's GPU vs the render GPU. */
static void exclusive_explain(HRESULT hr)
{{
    IDXGIDevice *dd = NULL;
    IDXGIAdapter *mine = NULL;
    IDXGIFactory *fac = NULL;
    IDXGIAdapter *ad = NULL;
    DXGI_ADAPTER_DESC md, od;
    HMONITOR mon;
    MONITORINFOEXW mi;
    UINT a, o;
    int found = 0;

    if (hr != (HRESULT)0x887A0004L || !s_dev || !s_hwnd)   /* DXGI_ERROR_UNSUPPORTED */
        return;
    mon = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, (MONITORINFO *)&mi);
    if (FAILED(ID3D11Device_QueryInterface(s_dev, &IID_IDXGIDevice, (void **)&dd)) || !dd)
        return;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dd, &mine)) && mine &&
        SUCCEEDED(IDXGIAdapter_GetDesc(mine, &md)) &&
        SUCCEEDED(IDXGIAdapter_GetParent(mine, &IID_IDXGIFactory, (void **)&fac)) && fac) {{
        for (a = 0; !found && IDXGIFactory_EnumAdapters(fac, a, &ad) != DXGI_ERROR_NOT_FOUND; a++) {{
            IDXGIOutput *out = NULL;
            for (o = 0; !found && IDXGIAdapter_EnumOutputs(ad, o, &out) != DXGI_ERROR_NOT_FOUND; o++) {{
                DXGI_OUTPUT_DESC d;
                if (SUCCEEDED(IDXGIOutput_GetDesc(out, &d)) && d.Monitor == mon &&
                    SUCCEEDED(IDXGIAdapter_GetDesc(ad, &od)))
                    found = 1;
                IDXGIOutput_Release(out);
            }}
            IDXGIAdapter_Release(ad);
        }}
        if (found && memcmp(&od.AdapterLuid, &md.AdapterLuid, sizeof od.AdapterLuid) != 0)
            fprintf(stderr, LOGP "exclusive fullscreen: monitor %ls is wired to %ls, but the game renders on %ls;"
                    " DXGI only allows exclusive on the render GPU's own monitors. Move the window to a monitor on"
                    " that GPU, or pick the GPU in Windows Settings > Display > Graphics. Borderless works on any"
                    " monitor.\n", mi.szDevice, od.Description, md.Description);
        else
            fprintf(stderr, LOGP "exclusive fullscreen: unsupported on monitor %ls (render GPU %ls, %s)\n",
                    mi.szDevice, md.Description, found ? "same GPU" : "no GPU lists this monitor");
    }}
    if (fac)
        IDXGIFactory_Release(fac);
    if (mine)
        IDXGIAdapter_Release(mine);
    IDXGIDevice_Release(dd);
}}

'''


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s:
        print("already patched")
        return
    nl = "\r\n" if "\r\n" in s else "\n"
    anchors = [
        "static void exclusive_service(void)",
        "        if (s_ex_tries == 1 || s_ex_tries == EX_MAX_TRIES)" + nl
        + '            fprintf(stderr, LOGP "exclusive fullscreen failed hr=0x%08lX (try %u of %u,"',
    ]
    for a in anchors:
        if s.count(a) != 1:
            raise SystemExit("REFUSED: anchor not found exactly once (run patch_d3d11_exclusive.py first): %r" % a[:60])
    s = s.replace(anchors[0], FUNC.format(mark=MARK).replace("\n", nl) + anchors[0], 1)
    s = s.replace(anchors[1], "        if (s_ex_tries == 1)" + nl
                  + "            exclusive_explain(hr); /* " + MARK + " */" + nl + anchors[1], 1)
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("patched d3d11 exclusive failure reason")


if __name__ == "__main__":
    main()
