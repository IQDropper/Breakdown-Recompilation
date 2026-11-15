"""patch_alloc_pages.py - idempotent. Toolkit fix: NtAllocateVirtualMemory rounds the size up to whole 4 KB pages.

RegionSize is IN/OUT on the real Xbox kernel and comes back rounded up to whole pages. Callers rely on the rounded
figure: the XAPI heap (RtlAllocateHeap's large-block path) stores it in the block header and later carves the tail.
Breakdown asks for 0x12F0030; the real kernel returns 0x12F1000. Returning 0x12F0030 skewed the heap and
VsHeap_New crashed later in Game_Init. Allocating the rounded size also keeps the next block from overlapping that
tail. Found by an xemu-vs-native memory diff; Foster's build had it by hand, fresh installs did not (2026-11-15).
Run by APPLY_BASE_PATCHES.cmd. Refuses (exit 1) if the code moved."""
import io, os, pathlib, sys

ROOT = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent)
P = ROOT / "external" / "xboxrecomp" / "src" / "kernel" / "kernel_bridge.c"
MARK = "/* Page granularity, like the real kernel: RegionSize is IN/OUT and comes back"

s = io.open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"
if MARK in s:
    print("kernel_bridge.c: already patched")
    sys.exit(0)

anchor = """    if (size == 0) {
        g_eax = 0xC0000045u; /* STATUS_INVALID_PAGE_PROTECTION */
        return;
    }
""".replace("\n", nl)
if s.count(anchor) != 1:
    print("[error] kernel_bridge.c: NtAllocateVirtualMemory size check not found (%d); patch by hand" % s.count(anchor))
    sys.exit(1)
add = """
    /* Page granularity, like the real kernel: RegionSize is IN/OUT and comes back
     * rounded up to whole 4 KB pages. Callers rely on the rounded figure -- the
     * XAPI heap (RtlAllocateHeap's large-block path) stores it in the block header
     * and later carves the tail. Breakdown: asked 0x12F0030, real kernel returns
     * 0x12F1000; returning 0x12F0030 skewed the heap, and VsHeap_New crashed later
     * in Game_Init. Allocating the rounded size also keeps the next block from
     * overlapping that tail. (Found by xemu-vs-native memory diff.) */
    size = (size + 0xFFFu) & ~0xFFFu;
""".replace("\n", nl)
s = s.replace(anchor, anchor + add)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("kernel_bridge.c: patched (NtAllocateVirtualMemory rounds to 4 KB pages)")
