#!/usr/bin/env python3
"""Reapply the NV2A/DSOUND spin-wait force-fixes to freshly regenerated
src/recomp/gen/*.c.

These are temporary, hand-applied patches (NOT yet engine-level fixes),
documented in HeroLab task 1c4b9a06-bbe5-4c94-9c34-aef7ef854cc7. Each one
forces a "the GPU/audio backend never signals completion" wait to resolve
immediately, because this recomp has no hardware emulation that would ever
clear the condition for real.

./regenerate.sh WIPES src/recomp/gen/*.c on every run. Always run this
immediately afterwards, before rebuilding. Skipping it reproduces what looks
exactly like a fresh regression -- the main loop plateaus at a low iteration
count -- but is only these fixes having been silently dropped. That mistake
cost real debugging time twice in the 2026-09-05 session.

Usage:  ./regenerate.sh && python3 apply_spin_fixes.py && \
        (cd build-win && cmake --build .)

--- Why fixes are anchored by ADDRESS ---

An earlier version of this script matched on literal excerpts of the
generated C. That made the file a derivative of the game's own code, which
this repository's .gitignore explicitly forbids from entering git history.

Anchoring on a label address instead keeps the script original work: a guest
VA is a fact about the binary, in the same category as an address in a bug
report, and the inserted statements are ours. The generated labels are
`loc_<VA>: ;` at the start of a line, so the anchor is exact.

--- Why every chunk, and every occurrence ---

Fixes used to name the chunk file they lived in. Any regeneration that adds
functions moves the split points, and the 2026-09-13 disassembler passes
added about a thousand, so every fix quietly missed its file.

A label is also no longer unique across the lift. An alias entry -- a second
way into a function, lifted as its own body running to the enclosing end --
repeats the code after its entry, labels included. The spin wait is the same
wait whichever copy the game enters through, so each copy gets the fix.
"""
import glob
import sys

GEN = "src/recomp/gen"

# (label VA, statements to skip after the label, statement, why)
#
# `skip` counts generated statement lines between the label and the insertion
# point. 0 means "immediately after the label".
FIXES = [
    (0x001C0880, 0, "MEM32(eax + 0x100410) &= ~0x10000u;",
     "no NV2A emulation ever clears this GPU status bit"),
    (0x001C0970, 0, "MEM32(eax + 0x100410) &= ~0x10000u;",
     "no NV2A emulation ever clears this GPU status bit"),
    (0x001C0A20, 0, "MEM32(eax + 0x100410) &= ~0x10000u;",
     "no NV2A emulation ever clears this GPU status bit"),
    (0x001D41A3, 0, "MEM32(eax + 0x10) = 0;",
     "no NV2A emulation ever clears this GPU push-buffer slot; simulate instant completion"),
    (0x001C6B40, 1, "MEM32(edi + 0x100) &= ~eax;",
     "no NV2A emulation ever clears these GPU status bits"),
    # Was an in-line edit of the preceding assignment; an ANDN on the next
    # line is equivalent and needs no excerpt of the generated statement.
    (0x001C72B0, 2, "esi &= ~0x1000000u;",
     "no NV2A emulation ever clears this GPU status bit"),
    (0x001D815F, 0, "MEM32(eax) &= ~0x100000u;",
     "no NV2A emulation ever clears this GPU status bit"),
    (0x0018EA80, 0, "MEM32(0x435800) = 0;",
     "no NV2A/audio backend ever clears this pending-count"),
    # 0x0018EAC4 RETIRED 2026-10-04. It forced the main loop's vblank counter
    # (0x434F90) to its target, so the loop never waited. The premise is gone:
    # the title's vblank callback sub_0018C880 now fires 60/s. Breakdown asks
    # for 2 vblanks per frame in levels (0x434F60), i.e. its original 30 fps;
    # with this fix in place levels ran 48-60 loops/s with a jittery step.
    # Testing stays at the original rate. The code for an optional higher
    # rate is parked in tools/frame-pacing/ (HeroLab tasks 9b169429, 3c72a998).
    (0x0018EC04, 0, "MEM32(0x43517C) = 0;",
     "nothing ever decrements this in-flight-frame counter"),
    (0x001C0C47, 3, "if (edi > eax) edi = eax;",
     "no NV2A emulation ever advances the GPU GET pointer at [edx]"),
    (0x001C0C6F, 0, "eax = 0;",
     "force the retry-until-success ICALL to be treated as having succeeded first try"),
    # D3DDevice_SetGammaRamp. Breakdown passes flags 0, "apply at the next
    # swap": the vblank handler (sub_001C69B0) writes the ramp while it
    # retires a queued flip. Frames here are presented by the D3D11 backend,
    # so that queue is always empty and a deferred ramp is never written.
    # D3DSGR_IMMEDIATE (2) writes it to the DAC palette at once, one vblank
    # early. Without it the title's darkening ramp (64->32, 128->82) is lost.
    (0x001BD840, 0, "MEM32(esp + 4) |= 2;",
     "no queued flip ever applies a deferred gamma ramp; apply it immediately"),
]


def fix_line(va, stmt, why):
    """The exact line this fix inserts. Carries the VA so it is both
    self-identifying in a diff and usable as the idempotency check."""
    return f"    {stmt} /* TEMP FIX 0x{va:08X}: {why} */"


def apply_all():
    applied = 0
    already = 0
    problems = []
    occurrences = {va: 0 for va, _skip, _stmt, _why in FIXES}

    paths = sorted(glob.glob(f"{GEN}/recomp_*.c"))
    if not paths:
        problems.append(f"no generated chunks under {GEN}")

    for path in paths:
        with open(path) as f:
            lines = f.read().split("\n")

        located = []
        for va, skip, stmt, why in FIXES:
            want = fix_line(va, stmt, why)
            label = f"loc_{va:08X}: ;"
            for hit in (i for i, l in enumerate(lines) if l.rstrip() == label):
                occurrences[va] += 1
                idx = hit + 1 + skip
                # A rerun finds its own line where it would insert.
                if idx < len(lines) and lines[idx] == want:
                    already += 1
                    continue
                if idx > len(lines):
                    problems.append(f"{path}: insertion point for 0x{va:08X} "
                                    f"past end of file")
                    continue
                located.append((idx, want))

        # Bottom of the file upwards, so an insertion cannot shift the line
        # numbers of the ones still to come.
        for idx, want in sorted(located, reverse=True):
            lines.insert(idx, want)
            applied += 1

        if located:
            with open(path, "w") as f:
                f.write("\n".join(lines))

    for va, count in occurrences.items():
        if count == 0:
            problems.append(f"label loc_{va:08X} not found in any chunk")

    copies = sum(occurrences.values())
    print(f"applied {applied} fixes, {already} already present, "
          f"{len(FIXES)} fixes at {copies} site(s), {len(problems)} problem(s)")
    for p in problems:
        print(f"  PROBLEM: {p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(apply_all())
