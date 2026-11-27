"""patch_horplus_menu.py - idempotent. Hor+: trim depth-tested menu quads to the 4:3 picture too (grey panel, Foster).

With the wide view on, the main menu showed a grey panel left of the 4:3 picture: a menu piece that starts about 80 guest
pixels off the left screen edge, where the Xbox cut it off. Hor+ scissors 2D draws (depth test off) to the 4:3 box, but
this panel is drawn with depth test on (seen running 2026-11-27: on the main menu every depth-tested draw is a 4-index
quad or fan, LEQUAL). In play, a frame draws the 3D world as meshes first; particles and sprites (also small quads with
depth test) come after them. So: until a frame has drawn a depth-tested mesh with more than 8 indices, a depth-tested
draw of at most 8 indices is a menu piece and is cut at the 4:3 box like other 2D. Needs patch_d3d11_horplus.py."""
import io
import os
import pathlib

ROOT = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent)
P = ROOT / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[horplus-menu]"
s = io.open(P, encoding="utf-8", newline="").read()
if MARK in s:
    print("already patched")
    raise SystemExit
if "[d3d11-horplus]" not in s:
    raise SystemExit("run patch_d3d11_horplus.py first")
nl = "\r\n" if "\r\n" in s else "\n"


def sub(old, new, what):
    global s
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    assert s.count(old) == 1, what
    s = s.replace(old, new, 1)


sub("static int s_hp_sq;               /* this draw was squeezed */\n",
    "static int s_hp_sq;               /* this draw was squeezed */\n"
    "static int s_hp_3d;               /* " + MARK + " this frame drew a depth-tested mesh into the main frame */\n",
    "state")
sub("        if (s_hp_sq && !v->depth_enable) { /* [d3d11-horplus] 2D: cut at the 4:3 box, as the screen edge did */\n",
    "        if (s_hp_sq && v->depth_enable && d->idx_count > 8)\n"
    "            s_hp_3d = 1;   /* " + MARK + " the 3D world has started: later small quads are particles */\n"
    "        if (s_hp_sq && (!v->depth_enable || (!s_hp_3d && d->idx_count <= 8))) { /* [d3d11-horplus] 2D: cut at the 4:3 box, as the screen edge did; " + MARK + " also menu quads with depth test */\n",
    "scissor")
sub("    s_presents++;\n",
    "    s_presents++;\n    s_hp_3d = 0;   /* " + MARK + " */\n",
    "present")
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("patched")
