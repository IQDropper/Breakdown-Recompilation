"""patch_d3d11_stock_size.py - idempotent. Render scale 1 fills the window again.

d3d11_flip has a "stock" path for render scale 1 + nearest + windowed: it copies the guest's frame into the back buffer
unscaled. That is right only when the back buffer is the frame's size (a 640x480 window). At the default window scale
2 the back buffer is 1280x960, so the 640x480 frame landed in its top-left quarter with the rest black (seen running,
GO kit ISSUES 2026-11-12 08:15; MEASURED S1.1 "picture drawn top-left").
This adds one condition: stock only when the back buffer is exactly the frame's host size. Otherwise the frame goes
through present_blit like every other setting (point sampling for nearest, letterboxed by nv2a_present_rect).
Anchors must each occur once; refuses otherwise."""
import io
import os
import pathlib

P = pathlib.Path(os.environ.get("BREAKDOWN_D3D11_FILE")
                 or pathlib.Path(__file__).resolve().parent / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c")
MARK = "[d3d11-stock-size]"


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s:
        print("already patched")
        return
    nl = "\r\n" if "\r\n" in s else "\n"
    old = ("                 && s_back_w == s_made_w && s_back_h == s_made_h;" + nl)
    if s.count(old) != 1:
        raise SystemExit("REFUSED: stock-path anchor not found exactly once")
    new = ("                 && s_back_w == s_made_w && s_back_h == s_made_h" + nl
           + "                 /* " + MARK + " unscaled copy only into a frame-sized back buffer */" + nl
           + "                 && s_back_w == s_drawn->hw && s_back_h == s_drawn->hh;" + nl)
    s = s.replace(old, new, 1)
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("patched d3d11 stock path size check")


if __name__ == "__main__":
    main()
