"""patch_d3d11_window.py - idempotent. D3D11 window client size.

The backend opens a 640x480 window. If the game has set
breakdown_display_window_size_hook (src/display_host.c), init() asks it for
the client size so RECOMP_WINDOW_SCALE and RECOMP_DISPLAY_ASPECT letterbox.
The hook pointer is defined here. Toolkit smoke tests link xbox_d3d8 without
the game, leave the pointer NULL, and keep 640x480. A hard extern used to
fail that link, and a stale build-win cache could omit display_host.c from
the exe for the same undefined symbol.

Upgrades a tree already patched with the hard extern. Refuses if init() moved.
"""
import io
import pathlib

P = pathlib.Path("external") / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[disp-window-opt]"
OLD_MARK = "[disp-window]"


def drop_stale_game_cache():
    """A build-win configured before display_host.c was a source will not
    compile it until cmake runs again. The CLI skips configure when
    CMakeCache.txt exists, and a copied tree can keep an older ninja file
    than CMakeLists.txt. Drop the cache only; object files stay."""
    root = pathlib.Path("build-win")
    ninja = root / "build.ninja"
    cache = root / "CMakeCache.txt"
    if not ninja.is_file() or not cache.is_file():
        return
    text = ninja.read_text(encoding="utf-8", errors="replace")
    if "display_host.c" in text:
        return
    cache.unlink()
    print("build-win was configured without src/display_host.c.")
    print("Removed build-win/CMakeCache.txt so the next breakdown build reconfigures.")
    print("Object files were kept. If cmake then errors, delete build-win and configure again.")


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s:
        print("already patched")
        drop_stale_game_cache()
        return
    nl = "\r\n" if "\r\n" in s else "\n"
    hook = (
        "/* "
        + MARK
        + " Optional. display_host_init sets this. Smoke tests leave it NULL. */"
        + nl
        + "void (*breakdown_display_window_size_hook)(unsigned int *w, unsigned int *h);"
        + nl
        + nl
    )
    call = (
        "    /* "
        + MARK
        + " NULL unless the game sets the hook (src/display_host.c)."
        + nl
        + "     * Toolkit smoke tests link xbox_d3d8 without the game and keep 640x480. */"
        + nl
        + "    if (breakdown_display_window_size_hook)"
        + nl
        + "        breakdown_display_window_size_hook(&bw, &bh);"
        + nl
    )
    init = "static int init(void)"
    if s.count(init) != 1:
        raise SystemExit("toolkit changed - patch nv2a_pb_d3d11.c init() by hand")

    old_call = (
        "    /* [disp-window] RECOMP_WINDOW_SCALE / RECOMP_DISPLAY_ASPECT (src/display_host.c). */"
        + nl
        + "    {"
        + nl
        + "        extern void breakdown_display_window_size(unsigned int *w, unsigned int *h);"
        + nl
        + "        breakdown_display_window_size(&bw, &bh);"
        + nl
        + "    }"
        + nl
    )
    fresh = (
        "    RECT r = {0, 0, 640, 480};"
        + nl
        + "    int full = nv2a_host_opts()->fullscreen;"
        + nl
        + "    UINT bw = 640, bh = 480;"
    )
    fresh_new = (
        "    UINT bw = 640, bh = 480;"
        + nl
        + "    int full;"
        + nl
        + "    RECT r;"
        + nl
        + call
        + "    full = nv2a_host_opts()->fullscreen;"
        + nl
        + "    r.left = 0; r.top = 0; r.right = (LONG)bw; r.bottom = (LONG)bh;"
    )

    if old_call in s:
        if s.count(old_call) != 1:
            raise SystemExit("old disp-window call is not unique - patch by hand")
        s = s.replace(old_call, call, 1)
        print("upgraded hard extern to optional hook")
    elif fresh in s:
        if s.count(fresh) != 1:
            raise SystemExit("toolkit changed - patch nv2a_pb_d3d11.c init() window size by hand")
        s = s.replace(fresh, fresh_new, 1)
        print("patched d3d11 window size")
    else:
        raise SystemExit(
            "toolkit changed - neither the stock window size nor the old disp-window call was found"
        )

    if "breakdown_display_window_size_hook)" not in s.split(init, 1)[0]:
        # definition must be before init(); the call mentions the name too.
        s = s.replace(init, hook + init, 1)
    if s.count(MARK) < 2 or "void (*breakdown_display_window_size_hook)" not in s:
        raise SystemExit("internal error: hook definition missing after patch")
    if "extern void breakdown_display_window_size(" in s:
        raise SystemExit("internal error: hard extern still present")
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    drop_stale_game_cache()


if __name__ == "__main__":
    main()
