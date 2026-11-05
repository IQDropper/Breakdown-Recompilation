"""patch_d3d11_window.py - idempotent. D3D11 window client size.

The backend opens a 640x480 window. breakdown_display_window_size (src/display_host.c)
applies RECOMP_WINDOW_SCALE and RECOMP_DISPLAY_ASPECT before that, so a 16:9 or
16:10 client letterboxes the 4:3 frame instead of leaving a tiny 4:3 window.
Fullscreen still replaces the size with the monitor. Refuses if init() moved.
"""
import io
import pathlib

P = pathlib.Path("external") / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
s = io.open(P, encoding="utf-8", newline="").read()
if "[disp-window]" in s:
    print("already patched")
    raise SystemExit(0)
nl = "\r\n" if "\r\n" in s else "\n"
old = (
    "    RECT r = {0, 0, 640, 480};" + nl
    + "    int full = nv2a_host_opts()->fullscreen;" + nl
    + "    UINT bw = 640, bh = 480;"
)
new = (
    "    UINT bw = 640, bh = 480;" + nl
    + "    int full;" + nl
    + "    RECT r;" + nl
    + "    /* [disp-window] RECOMP_WINDOW_SCALE / RECOMP_DISPLAY_ASPECT (src/display_host.c). */" + nl
    + "    {" + nl
    + "        extern void breakdown_display_window_size(unsigned int *w, unsigned int *h);" + nl
    + "        breakdown_display_window_size(&bw, &bh);" + nl
    + "    }" + nl
    + "    full = nv2a_host_opts()->fullscreen;" + nl
    + "    r.left = 0; r.top = 0; r.right = (LONG)bw; r.bottom = (LONG)bh;"
)
if s.count(old) != 1:
    raise SystemExit("toolkit changed - patch nv2a_pb_d3d11.c init() window size by hand")
io.open(P, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
print("patched d3d11 window size")
