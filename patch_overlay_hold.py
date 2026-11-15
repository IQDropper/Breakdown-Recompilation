"""patch_overlay_hold.py - idempotent. D3D11 exclusive fullscreen steps aside while a host overlay is shown.

The debug menu text (src/debug_text_host.c) and the other host overlays are separate see-through windows over the
game window. DXGI exclusive fullscreen shows only the game's own swap chain, so in exclusive mode the debug menu was
a dark screen with no text (a player report, 2026-11-15; reproduced: windowed and borderless show the menu,
exclusive shows nothing).

Adds breakdown_display_overlay_hold(int on): while held, exclusive_service() treats exclusive as not wanted, so the
flip leaves exclusive (the window stays at its size); releasing it lets the flip enter exclusive again.

Run after patch_d3d11_exclusive.py (APPLY_DISPLAY_PATCHES.cmd). Refuses if an anchor moved.
BREAKDOWN_ROOT points it at another copy (tests)."""
import io
import os
import pathlib
import sys

ROOT = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent)
P = ROOT / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[overlay-hold]"

s = io.open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"
if MARK in s:
    print("nv2a_pb_d3d11.c: already patched [overlay-hold]")
    sys.exit(0)
if "void breakdown_display_set_exclusive(int on)" not in s:
    print("[error] run patch_d3d11_exclusive.py first")
    sys.exit(1)


def sub(old, new, what):
    global s
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if s.count(old) != 1:
        print("[error] toolkit changed - %s not found exactly once (%d)" % (what, s.count(old)))
        sys.exit(1)
    s = s.replace(old, new, 1)


sub("""/* The game asked for exclusive again (or focus came back): retry now. */
void breakdown_display_set_exclusive(int on)""",
    """/* [overlay-hold] Host overlays (debug menu text, PC options...) are separate windows, which exclusive
 * fullscreen hides. While one is shown the host holds exclusive off; the flip re-enters it on release. */
static volatile int s_overlay_hold;
void breakdown_display_overlay_hold(int on)
{
    s_overlay_hold = on ? 1 : 0;
}

/* The game asked for exclusive again (or focus came back): retry now. */
void breakdown_display_set_exclusive(int on)""", "breakdown_display_set_exclusive")
sub("    want = nv2a_host_opts()->fullscreen >= 2;\n",
    "    want = nv2a_host_opts()->fullscreen >= 2 && !s_overlay_hold;   /* [overlay-hold] */\n", "want = ... >= 2")
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("nv2a_pb_d3d11.c: patched [overlay-hold] (exclusive steps aside while a host overlay is shown)")
