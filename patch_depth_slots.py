"""patch_depth_slots.py - idempotent. D3D11 backend: keep up to 16 depth buffers instead of 4, and log evictions.

Breakdown draws with at least six depth buffers in one frame: the 640x480 scene (zeta 0x00520000), 512x512 and 256x256
shadow/light buffers (0x01000000, 0x01080000, 0x01100000, 0x01120000) and a 256x128 one (0x01182000). With 4 slots the
least-recently-used one was released and re-created, and a re-created buffer starts cleared to the far plane. Foster's
2026-11-27 play log: the scene's depth buffer was re-created 8,091 times in 10 minutes, swapping with 0x01120000. The
walls are drawn after a depth pre-pass with a depth-EQUAL test, so a wiped scene buffer drew them black (worst when a
gun's muzzle light adds a light pass), and wiped shadow buffers made shadows flicker."""
import io
import os
import pathlib

ROOT = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent)
P = ROOT / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[depth-slots]"
s = io.open(P, encoding="utf-8", newline="").read()
if MARK in s:
    print("already patched")
    raise SystemExit
nl = "\r\n" if "\r\n" in s else "\n"

old = "#define DT_MAX 4" + nl
assert s.count(old) == 1, "DT_MAX"
s = s.replace(old, "#define DT_MAX 16   /* " + MARK + " the game uses 6+ zeta buffers per frame; 4 thrashed */" + nl, 1)

old = ("    if (!e) {" + nl + "        e = lru;" + nl +
       "        ID3D11DepthStencilView_Release(e->dsv);")
assert s.count(old) == 1, "evict"
new = ("    if (!e) {" + nl + "        e = lru;" + nl +
       "        { static unsigned long n_evict; /* " + MARK + " its contents are lost: should not happen now */" + nl +
       "          if (n_evict++ < 20)" + nl +
       "              fprintf(stderr, LOGP \"zeta 0x%08X: depth buffer %ux%u evicted for 0x%08X, its depth lost (DT_MAX %d full)\\n\"," + nl +
       "                      e->zeta, e->w, e->h, zeta, DT_MAX); }" + nl +
       "        ID3D11DepthStencilView_Release(e->dsv);")
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("patched")
