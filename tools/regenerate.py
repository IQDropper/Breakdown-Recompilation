#!/usr/bin/env python3
"""Regenerate the recompiled C for Breakdown from the user's own disc image.

This is the only invocation that reproduces a working build. The generic
sequence in the xboxrecomp toolkit's docs/GETTING_STARTED.md does NOT: it
omits --seed-functions, and without those seeds the result does not boot at
all. That knowledge previously lived only in a chat transcript and was very
nearly lost, which is why it lives here now.

Nothing this produces is redistributable -- src/recomp/gen/ is derived from
the game and is gitignored. Every user runs this against their own copy.

Python rather than shell so the Windows launcher can run it as well
(regenerate.sh is now a thin wrapper around this file).

Usage:  python tools/regenerate.py [--xbe PATH] [--toolkit DIR]
        then rebuild in build-win/
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Entry points reachable ONLY as data: each is stored to a pointer or handed to
# the kernel as a thread routine, never the target of a call or jump, so no
# amount of analysis in the disassembler can discover them. Each one was found
# the hard way; do not drop any of them.
#
#   0x001A8460  worker-thread start routine. Without it PsCreateSystemThreadEx
#               reports "start routine not found in dispatch", the thread never
#               runs and the boot dies two kernel calls in. Seeding it also
#               cascades ~280 further functions into reach.
#   0x001A7F72  the context argument handed to that routine.
#   0x001B6603  the CRT's fallback InitializeCriticalSectionAndSpinCount, which
#               always succeeds on real hardware. Unresolved, its indirect call
#               yields 0, the CRT reads that as a failed lock init, tears down
#               its whole lock table, and every later lock acquisition recurses
#               until the host stack overflows.
#   0x001B5F75  the CRT's per-thread attach/detach notification callback
#               (stdcall, 1 arg -- ends `ret 4`). sub_001B242F's _mtinit path
#               writes this address into a function-pointer slot at guest VA
#               0x451780 (`mov dword ptr [0x451780], 0x1b5f75`) but never
#               calls or jumps to it directly, so it was invisible to the
#               disassembler even though the sweep had already decoded its
#               bytes correctly (unlike 0x001B6603, this one needed only a
#               function-boundary entry, not a decode_at() rescue). Unresolved,
#               every new worker thread's attach notification silently no-ops,
#               which is consistent with the observed pattern before this seed
#               was added: threads kept spawning (up to the 8-slot cap, then
#               "out of thread stacks, running worker inline") instead of the
#               title settling into steady-state execution.
#   0x001AF8F5  a single `c3` (ret) byte -- the attach-notification target
#               sub_001B5F75 calls through slot 0x0028F58C. Sits in a 1-byte
#               gap between two already-recognized functions (a switch-style
#               dispatch block ending at 0x001AF8F0, and a real function
#               starting at 0x001AF8F6), so boundary detection never claimed
#               it for either neighbor. Found only after seeding 0x001B5F75
#               above let that call actually execute for the first time --
#               each seed here can reveal the next one; check icall_targets.dump
#               after every regen rather than assuming four is the final count.
#   0x001D5B6A  a NuSoundStream vtable method (in the DSOUND section -- see
#               below) that computes and writes this[0x64], a per-buffer
#               field a later divide (sub_001D67D7) uses as its divisor with
#               no zero-check (real hardware never reaches that divide with
#               it unset, so the original code never needed one). Same
#               data-only shape as the two seeds above: reachable only
#               through a vtable slot, never a static call/jmp. Unresolved,
#               this[0x64] stays at its zeroed default and the divide by it
#               faults with an unhandled SIGFPE-equivalent.
#   0x001D136C  \
#   0x001D1636   } three more DSOUND vtable methods of exactly the same
#   0x001D88AB  /  shape, found the same way (they showed up as repeated
#               "[ICALL] Failed to resolve VA ..." in a boot log, and
#               0x001D88AB fires immediately before a second instance of
#               the sub_001D67D7 divide-by-zero above -- a different voice
#               object reaching the same unchecked divide because ITS
#               this[0x64] setter never ran either). Each sits exactly at
#               the end boundary of the preceding function with a clean
#               entry prologue (0x001D88AB: `push esi; mov esi,ecx`;
#               0x001D136C: `mov eax,[esp+4]`), and each has ZERO xrefs of
#               any kind -- not even a data_imm -- because the only
#               reference is a vtable slot built at runtime.
#
# 0x001B6603 also needs xboxrecomp commit c70542c or newer: before it, seeding
# an address the linear sweep had stepped over silently did nothing.
#
# EXPERIMENTAL, part 10 bisection (HeroLab task 999ce5ca): the 7 addresses
# below are the .text-tail cluster at 0x001BD570-0x001BD7C0, immediately
# before the D3D section starts (0x001BD840). 4 of the 7 (660/670/680/7C0)
# are byte-for-byte MSVC static-init thunks: `mov ecx,<global>; jmp <ctor>`,
# 16-byte aligned, int3-padded to the next thunk. The other 3 (570/5C0/690)
# are NOT that same 10-byte shape but decode to coherent, non-garbage code
# consistent with other _initterm-table entries (a ctor+atexit-register
# wrapper, a float-constant cache-into-global routine, and an IAT-call +
# struct-zero-init routine) -- so grouped with the cluster on that basis.
# Verified by reading raw XBE bytes at each VA and disassembling with
# objdump; none of the 7 are garbage/misaligned reads. This is the
# suspected reason sub_00199760's hash-table object (this[0xC] = bucket
# count) is never constructed. Seeded ALONE (not with the other 8 pending
# unresolved-ICALL addresses, which are DSOUND/XPP stubs for unrelated
# globals and were part of the reverted 15-seed shotgun) specifically to
# test whether this cluster alone clears the div0 without triggering the
# VA-0x00000000 storm seen when all 15 were seeded together.
#
# RESOLVED (part 13): 0x001BD7C0 is a GOOD seed and is now in the list. The
# "storm" it appeared to cause was not its doing -- it is the thunk that
# constructs the engine's hash table, so seeding it lets the title get much
# further, far enough to carve ~59.7 MB of subsystem arenas. Our 64 MB memory
# map only offered 48.5 MB above XBOX_HEAP_BASE, because the bridges served
# MmAllocateContiguousMemory out of the general heap. The last arena request
# therefore failed, and the CRT's out-of-memory path tore its own lock table
# back down -- which is what produced the 122,000 null-vtable calls.
#
# Fixed in xboxrecomp by routing MmAllocateContiguousMemory(Ex) to the
# dedicated window at XBOX_CONTIG_BASE (xbox_ContigAlloc). The ~31 MB of big
# buffers now live at 0x82048000-0x83F35000, the heap peaks around 3.9 MB, and
# the title runs on the RETAIL 64 MB map with no divide-by-zero, no storm and
# no exhaustion. No RAM-size override is needed; if you find one in main.c,
# it is stale.
#
# BISECTION RESULT (part 10, superseded by the above -- kept because the
# measurements are still true, only the conclusion drawn from them was wrong):
#   0x001BD660,670,680,7C0 (the 4 clean thunks)        -> STORM
#     (242,425 log lines, 120,641 calls to VA 0x00000000, CPU 116%)
#   0x001BD660,670,680 alone                            -> CLEAN
#     (1,184-1,202 log lines, CPU ~1.9%, 0 VA-0 calls, div0 STILL PRESENT
#      -- sub_00199760, unchanged from the 9-seed baseline)
#   0x001BD660,670 alone                                -> also CLEAN
#     (same numbers as above)
# So of the four byte-identical thunks, 0x001BD7C0 is what turns the
# storm on -- it alone, added to the otherwise-clean 660/670/680 set, is
# the delta between the last clean run and the first storming one. NOT
# independently re-tested in isolation (budget) -- this is the strongest
# read of the data, not a fully isolated confirmation. The three
# differently-shaped candidates (570/5C0/690) were never seeded in this
# round either; they remain untested.
#
# CONCLUSION (as amended by part 13 above): seeding 660/670/680 is a strict
# improvement over the 9-seed baseline. 0x001BD7C0 was blamed for the VA-0
# storm; it was actually the first seed to make the title progress far enough
# to run our memory map out of heap, and the storm was the CRT's OOM path.
# With the 128 MB map it is a strict improvement too, and it is what finally
# clears sub_00199760's divide-by-zero -- so the static-init hypothesis for
# that hash table is now CONFIRMED, not unproven.
#
# 570/5C0/690 (the three differently-shaped candidates) remain untested; add
# them one at a time if more unresolved ICALL targets need clearing.
#
# 0x0001FD40, 0x00033130, 0x000AF960 (HeroLab task 9369e569): confirmed via
# icall_targets.dump to be dispatched from INSIDE the steady main loop the
# title reaches after the part-14 fixes, interleaved with resolved targets --
# each silently returns eax=0 today, so the loop's state machine may never
# advance. Same shape as every prior vtable-only find: each is referenced
# ONLY as data_imm (xrefs.json), never a static call/jmp target, and each
# decodes to clean non-garbage code at its VA (verified with loader.py +
# capstone against the raw XBE bytes) -- 0x0001FD40 and 0x000AF960 are float
# accumulator routines that both call a shared helper at 0x001AF944;
# 0x00033130 is a 5-byte `jmp 0x00033070` thunk (int3-padded) back into the
# start of the immediately preceding function, itself only reachable via a
# data_imm slot at 0x00033170.
#
# 0x00026760: surfaced ONLY after the three seeds directly above went in --
# resolving them let the main loop run far enough to reach a new indirect
# call site (0x000267B7, data_imm) that was never exercised before. Same
# shape again: push ebx/esi prologue, two direct calls, clean `ret`,
# int3-padded. Confirms the "each seed can reveal the next one" pattern
# from the earlier worker-thread/attach-notification chain -- check
# icall_targets.dump after every regen rather than assuming a given batch
# is the last one needed.
#
# 0x001D1E08, 0x001D558F (HeroLab task 1c4b9a06, 2026-09-05): surfaced only
# after the main-loop-repetition fix let the title get far enough to start
# loading D:\romdata\movie\namcologo.xmv (the Namco splash movie) -- its
# audio track exercises DSOUND vtable slots never hit before. Both in the
# DSOUND section (already covered by --extra-sections DSOUND below), both
# verified via loader.py+capstone against the raw XBE bytes to decode as
# clean, non-garbage code, zero xrefs of any kind (vtable-only, same shape
# as every seed above):
#   0x001D1E08: `call sub_001D14F5; cmp [0x1DB498],0; ...; call [0x20BBA4]`
#     -- byte-for-byte the same init-guard/E_FAIL shape as the already-seeded
#     0x001D88AB/0x001D136C/0x001D1636 (sub_001D3909 in the recompiled C is
#     this exact shape too) -- another sibling vtable method of the same
#     class, not a new mechanism.
#   0x001D558F: `mov eax,[esp+4]; shl eax,5; lea eax,[eax+ecx+0xD0]; mov
#     [eax+0x18],edx; ...` -- indexes a 32-byte-stride array and sets three
#     adjacent fields (+0x14/+0x18/+0x1C), consistent with a per-voice
#     parameter setter (position/pan-shaped) in the same DSOUND object
#     family as the other DSOUND seeds.
#
# 0x001AF92E, 0x00204907, 0x00206456 (HeroLab task 1c4b9a06, 2026-09-05):
# surfaced in the SAME regen/test pass as the two DSOUND seeds directly
# above -- resolving those let the movie-loading path run further and hit
# three more unresolved ICALLs. All three verified via loader.py+capstone
# to decode as clean, non-garbage code, zero xrefs:
#   0x001AF92E: `call sub_001AF8F6; cmp [esp+4],0; je +5; call sub_001B19A4;
#     fnclex; ret; int3; int3` -- a tiny FPU-exception-clear wrapper sitting
#     in the int3 gap immediately before the already-seeded/known
#     sub_001AF944 (the frame-timing helper the main loop's tail calls every
#     pass). Only calls two ordinary .text functions, so no further
#     recursive-descent concern.
#   0x00204907, 0x00206456: BOTH in the XPP section, which is NOT currently
#     in --extra-sections below. Seeded directly rather than adding XPP
#     wholesale -- same as the 0x001BD66x/70/80/7C0 cluster earlier in this
#     file, none of which needed their containing section added either,
#     because a seed forces engine.decode_at() regardless of which section
#     it's in. Neither function contains a CALL to anything else (both are
#     small, self-contained field-initializer/constructor shapes -- 0x206456
#     is literally `mov eax,ecx; mov byte[eax],0xFF; mov byte[eax+1..3],0x80;
#     ret` -- a default-RGBA-ish constant setter), so there is no recursive
#     descent into more of XPP to worry about from these two specifically. If
#     a *future* unresolved-ICALL address turns out to need a CALL out to
#     more of XPP, add `--extra-sections DSOUND,XPP` below instead of
#     seeding one at a time.
# 0x001D5DB5, 0x001DBD74 (HeroLab task 1c4b9a06, 2026-09-05): surfaced only
# after fixing the translator's flag-state merge bug (see xboxrecomp-fork
# tools/recomp/translator.py -- a `dec`/back-edge-`jmp` loop's own exit check
# was resolving to a dead `if (_flags)` fallback for 60 branches across this
# lift, one of them a genuine infinite loop that had been the real ceiling
# blocking all progress past main-loop iteration 13). With that fixed the
# title runs continuously for the first time -- 800k+ per-object dispatch
# calls in 31s, no plateau -- and immediately started hitting these two:
#   0x001D5DB5: `mov eax,[esp+4]; sub eax,0; je/dec eax chain (5-way switch
#     on a small integer) -> call one of sub_001D5D27/1D51D2/1D5991/1D5799`
#     -- byte-for-byte the same dispatcher shape as other seeded DSOUND
#     entries. Called 23,727 times in the same 31s window (once per audio
#     tick), confirming this is the DSOUND per-tick path, not a one-shot
#     init call. Its four call targets are ordinary direct `call`s to
#     addresses already inside the DSOUND section's decode range, so no
#     further recursive-descent concern.
#   0x001DBD74: real, non-garbage code in the XMV section -- checks a `this`
#     pointer, sets up buffer offsets at [eax+0x94]/[eax+0x98], calls
#     sub_001AAF3E and sub_001AAC4E (both ordinary .text addresses, already
#     in range), returns 1 or an E_FAIL/HRESULT-shaped code. This is the
#     first-ever sign of the title reaching into the XMV (movie) section at
#     all -- confirms the intro-movie codepath is now being exercised, not
#     just the file open. Verified via loader.py+capstone before seeding,
#     same as every seed above.
#
# 0x001B6849 (HeroLab task 1c4b9a06, 2026-09-05): surfaced immediately after
# the two seeds directly above -- with those resolved, the title ran the
# main loop continuously (900k+ per-object dispatch calls, no plateau) and
# reached this one ICALL failure, which cascaded into the game's own
# KeBugCheck(0) fatal-error path and a hard crash (ExitProcess(0) from a
# worker thread, surfaced by Wine as an unhandled-illegal-instruction
# prompt) -- exactly the "failed ICALL returns a plausible 0, caller treats
# it as fatal" pattern documented in the xboxrecomp-icall-dispatch skill.
# Trivial two-instruction .text thunk, `push [0x45195C]; call sub_1A83C9;
# ret` -- both the pushed global and the call target are ordinary, already-
# resolvable .text addresses, so this is reachable only as data (a vtable
# slot) with no further recursive-descent concern. Verified via
# loader.py+capstone before seeding.
#
# 0x0020A719 / 0x001DAB69 / 0x001D4F3F (2026-09-06): three interrupt service
# routines. Found only after the NV2A backend was wired up and the kernel
# trace threshold was raised past the boot thread's display init -- the
# bridge reported each as "KeConnectInterrupt: routine 0x... does not resolve
# to a function, not delivered", so the title registered them and nothing
# ever ran them. Classic reachable-only-as-data case: an ISR address is
# stored into a KINTERRUPT, never the target of a static call.
#   0x0020A719 (XPP)    -- real function; increments a counter at 0x45173C
#                          and walks the interrupt object's fields.
#   0x001DAB69 (DSOUND) -- `mov ecx,[esp+8]; call 0x1DAA3C; setne al; ret 8`
#   0x001D4F3F (DSOUND) -- `mov ecx,[esp+8]; call 0x1D4EA3; ret 8`
# The two `ret 8` thunks confirm the shape: stdcall, two parameters, i.e.
# KSERVICE_ROUTINE(PKINTERRUPT, PVOID). All three verified with
# loader.py+capstone -- ordinary code, ordinary call targets, no garbage.
# XPP needs no --extra-sections concern; the sweep already covers it.
#
# 0x001D4D5F (2026-09-06): the DPC routine those ISRs queue, found straight
# after seeding them -- 0x001DAB69/0x001D4F3F started running and the bridge
# then logged "KeInsertQueueDpc: routine 0x001D4D5F not resolvable" 3,687
# times in one 60s run. Seeding an ISR exposes whatever it queues, so expect
# to walk this chain a link at a time.
#   0x001D4D5F (DSOUND) -- `mov ecx,[esp+8]; call 0x1D4CC3; ret 0x10`
# `ret 16` is stdcall with four parameters, i.e.
# KDEFERRED_ROUTINE(PKDPC, PVOID DeferredContext, PVOID Arg1, PVOID Arg2).
# Verified with loader.py+capstone.
#
# 0x001D5A85, 0x001D8397 (2026-09-07, HeroLab task 28b5168a follow-up): the
# next link in that same chain, found the same way as every entry above --
# sub_001D4CC3 (queued by the 0x001D4D5F DPC, seeded just above) calls
# KeSynchronizeExecution in a loop, and that ordinal turned out to have NO
# bridge in xboxrecomp-fork's kernel_bridge.c at all (a silent "no bridge for
# ordinal 153" FALSE-return, same shape KeInsertQueueDpc itself had before
# ITS fix). Once the missing bridge was written (resolving SynchronizeRoutine
# via recomp_lookup and invoking it on the simulated stack, exactly like
# bridge_KeInsertQueueDpc already does for DeferredRoutine), the guest's own
# SynchronizeRoutine at 0x1D46C1 started running for the first time and
# immediately hit these two unresolved ICALLs, millions of times per second
# (the loop retries on every failure with nothing to make it back off).
# Both verified via loader.py+capstone against the raw XBE bytes to decode as
# clean, non-garbage code, both zero-xref (vtable-only, same shape as every
# DSOUND seed above -- and both share a call to the same helper, 0x1D50BA,
# with the earlier-seeded 0x001D558F/0x001D1E08 family):
#   0x001D8397: `push ebx; push esi; mov esi,ecx; push edi; lea edi,[esi+0x68];
#     push 3; mov ecx,edi; call 0x1D50BA; ...; call [eax+0x1C]` -- a per-object
#     service method with a vtable call at the end.
#   0x001D5A85: `push esi; mov esi,ecx; push edi; lea edi,[esi+0x68]; push 3;
#     mov ecx,edi; call 0x1D50BA; ...` -- sibling shape, same [esi+0x68]
#     sub-object and same shared helper.
# Not yet regenerated/tested past this pair -- per this file's own long-
# standing warning, check icall_targets.dump after the regen these seeds are
# part of rather than assuming two is the final count for this chain.
# 0x0017F320 was added 2026-09-08, found by the ICALL failure log naming it as
# the only unresolved indirect target left in a run (3,148 calls). The
# disassembler puts it in a clean 64-byte hole: sub_0017F260 ends at exactly
# 0x0017F320 and the next detected function starts at 0x0017F360, with nothing
# claiming the space between -- the signature of an entry point reachable only
# through a pointer, which is what this list is for.
#
# 0x0018C880 (2026-09-09): vblank callback installed by the title.
# It increments a guest counter and returns (cdecl). With the CRTC timer
# active, its missing dispatch entry causes fallback stack cleanup to run
# before caller cleanup, corrupting the GPU DPC stack by four bytes and
# leaving PMC interrupts disabled after three callbacks.
#
# 0x001D1F04: DSOUND vtable method at table 0x002262B0, reached
# after sustained vblank resumes. Runtime-confirmed missing target;
# the original body ends with ret 4 and calls existing audio helpers.
#
# Complete that eight-method vtable: 1D1E4F and 1D1F9C were the next
# runtime failures after the clock fix. 1D1E9D, 1D1F51 and 1D3A11 are
# its other missing entries, verified as real functions in the retail XBE.
#
# 0x001D1379: runtime-confirmed DSOUND indirect target reached after that
# complete audio vtable was available. It is the release-style sibling
# immediately following already-seeded 0x001D136C: clears [object+4], calls
# vtable slot zero, and ends with `ret 4`. Verified against the retail bytes.
#
# 0x001AF8E5 and 0x001AF8ED: adjacent data-only destructor-adjustor thunks.
# The first was runtime-confirmed after 0x001D1379 was resolved; the second is
# the same verified six/eight-byte shape in the neighboring slot. They adjust
# ECX by 4/8 and tail-jump to 0x001D2BCD/0x001D4D6B respectively.
#
# 0x001D5E00, 0x001D62DF, 0x001D63E5, 0x001D7E73, 0x001D8A14 and
# 0x001B1594: remaining missing entries in the retail method-table cluster at
# 0x002262F0-0x0022637C. 0x001D5E00 surfaced at runtime after the adjustors
# above were resolved. The five DSOUND entries are verified sibling deleting
# destructors ending in `ret 4`; 0x001B1594 is a clean no-argument callback.
#
# 0x001D36C0 and 0x001D2460: the final two missing deleting-destructor entries
# in the immediately preceding retail table cluster. 0x001D36C0 surfaced at
# runtime; both are verified DSOUND functions ending in `ret 4`.
#
# 0x0001B900, 0x00113630, 0x00129240, 0x00113110 (2026-09-13): four
# address-taken callbacks, each stored as an immediate and never called or
# jumped to directly, so they only show up as "[ICALL] Failed to resolve VA".
# All four were verified against the retail bytes with loader.py+capstone.
# Each starts right where a detected function ends, either at its terminator
# or after int3 padding.
#   0x0001B900: the front-end object's next update callback. sub_0001B9F0
#     installs it in [obj+0x20] once sub_00026460 (boot sequence) and
#     sub_00026440 (base.pak loader) both report done, and sub_0001B870
#     installs it too. `push ebx; push esi; mov esi,[esp+0xC]`, after the
#     int3 run that follows sub_0001B8E0's tail jmp. Unresolved, the game has
#     no scene after opening_b.xmv ends. It submits only its bloom chain and
#     the screen stays black; the call failed once per frame from boot state 8
#     on, 2,146 times in a 160 s run.
#   0x00113630: an update callback stored in [esi+0x20] at 0x001138B0.
#     `sub esp,0xC; push esi; mov esi,[esp+0x14]`, after int3 padding. It
#     failed about 7,000 times per run, from early boot on.
#   0x00129240: a callback stored in [eax+0x28] at 0x001292B8. It sets a
#     640x448 viewport (`push 448.0f; push 640.0f; ... call 0x18F2C0`) and
#     follows sub_00129200's `ret` with no padding. It failed 9,000-11,000
#     times per run, from early boot on.
#   0x00113110: `mov ecx,[esp+4]; mov al,1; mov [ecx+8],al; ret`, pushed as a
#     callback to sub_0017E4D0 from six sites. It failed once per run.
# There were many more in the same shape, so xboxrecomp-fork's disassembler now
# recovers address-taken functions itself: immediates that land in a gap and
# reach a ret, and code pointers held in data sections (ported from upstream's
# work/v0.7.0-non-local). Run without any seeds, it finds 50 of this list's 54
# entries, 0x00113630 and 0x00113110 among them. Keep the seeds anyway, as the
# note at the top says. Two of these four are still needed:
#   0x0001B900: its first ret is the 69th instruction, past the probe's
#     64-instruction window.
#   0x00129240: it ends in a tail jmp, and the immediate pass only accepts
#     bodies that reach a ret.
# 0x00033130 and 0x00204907 are the other two it cannot find.
#
# 0x0011C4C0, 0x00117870 (2026-09-13): two more of that same address-taken
# shape, reached for the first time when a Start press (through the new
# XInput overrides in src/recomp_manual.c) took the title screen to the main
# menu. Each logged one "[ICALL] Failed to resolve VA" on the way in, and both
# were verified against the retail bytes.
#   0x0011C4C0: taken as an immediate at 0x0011C621 and 0x0011C743. It starts
#     straight after sub_0011C450's `ret` and dispatches through a switch
#     (`jmp [esi*4+0x11C6A8]`) before its first ret. The immediate probe stops
#     at an indirect jmp, so the pass rejects it.
#   0x00117870: taken as an immediate at 0x0011D137, after int3 padding
#     (`push esi; mov esi,[esp+8]; mov al,[esi+0x94]`). Its first ret is the
#     97th instruction, past the probe's window.
#
# 0x00115650 (2026-09-13): the same shape again, reached when a live test
# chose NEW GAME from the main menu. Its one "[ICALL] Failed to resolve VA"
# left the screen black: frames kept rendering but nothing was ever loaded.
# It is an update callback taken as an immediate at 0x0011796D, after int3
# padding (`push esi; mov esi,[esp+8]; mov eax,[esi+0x34]; dec eax`). It
# switches on its state through `jmp [eax*4+0x1156FC]` before its first ret,
# which the immediate probe rejects, as it does 0x0011C4C0. Verified against
# the retail bytes.
#
# 0x00096CE0 (2026-09-13): two instructions between int3 padding --
# `mov eax,[esp+4]; jmp [eax*4+0x24B478]` -- a dispatcher that tail-jumps
# through a table of functions in .data, pushed as a callback to sub_000AB7E0
# and sub_000AB870. It has no ret at all, so no address-taken probe can accept
# it (the switch rule in xboxrecomp's probe only trusts tables inside the code
# section). Reached while the first area (hon_under) loads behind the opening
# credits after NEW GAME. Its failed call returned 0, and the caller read
# through a pointer built from that: an access violation at Xbox VA
# 0xFCFFFEC3. Every entry of the table it jumps through is already a
# recompiled function.
#
# 0x00024CB0 (2026-09-13): a five-byte thunk, `jmp 0x00018930`, between the
# previous function's switch table and int3 padding. The title stores it as an
# object's destroy callback (`mov [esi+0x24], 0x24CB0` at 0x0002565A), and
# it's the only immediate-taken jmp thunk of its kind in the XBE, which is not
# worth a rule of its own. Unresolved, it failed 13 times at once about 50 s
# into NEW GAME, and the game drew nothing from then on while it kept flipping
# frames.
#
# 0x000F5860 (2026-09-13): `mov eax,[esp+4]; xor ecx,ecx; jmp 0x000F5480`,
# named only by a four-entry callback table in .data at 0x00250510 (the
# other three entries already resolve). It sits straight after
# sub_000F5710's switch table, which the linear sweep decodes as instructions
# and comes out of misaligned, so the address is not an instruction boundary
# and the data-table pass cannot take it. A seed decodes there regardless.
# Unresolved, it failed twice while hon_under (the first level) loaded, and
# the game raised KeBugCheck(0) from 0x001B011F.
#
# 17 jump targets (2026-09-15) that only existed as stubs in
# recomp_stubs_unresolved.c. A stub pops 4 bytes and returns, which is right
# for none of them: they are tail-jump targets inside functions (epilogues
# such as 0x000CFB14 `pop esi; add esp,0xc; ret`) or starts the sweep missed.
# sub_000CF750, a vtable method called repeatedly in play, exits through
# 0x000CFB14, so each call leaked 16 bytes of guest stack. The stack sank
# past its slice into .data and overwrote the XAPI thread-notify node at
# 0x451778 (routine = 0x48), and every thread start/exit then looped on a
# null call: the play-test freeze. Found with [ESPGUARD] in recomp_manual.c.
# Six more stub addresses are not code in the disassembly and are left out.
#
# 0x0016FC60 (2026-09-15): a vtable method reached through the same
# dispatch as 0x000CF750, real code (`push esi; push edi; call 0x18c2d0`)
# that no call or jump names, so it had no stub either -- the indirect
# call just failed and returned 0. Seen 20 times in one play session.
#
# 0x001674B0 (2026-09-15): the same shape, and this one froze the game.
# sub_0017E280 calls it indirectly at 0x0017E309; the call failed, returned
# 0, and that thread was still spinning there when the session was killed.
#
#
# 0x000CF895 0x000CF8C3 0x000CF8F8 0x000CF943 0x000CF9C5 0x000CFA53
# (2026-10-04): the rest of sub_000CF750's switch. Its jump table at
# 0x000CFB1C has 13 entries, but the lifter recognised one ("switch: 1
# entries"), so every other case leaves through RECOMP_ITAIL by address.
# 0x000CF7E9 and 0x000CFB01/0F/14 already resolve (the latter seeded
# 2026-09-15); these six did not. Each case ends in the parent's own epilogue
# (pop esi; add esp,0xc; ret), the same shape as the earlier seeds.
# 0x000CF895 failed when the player left the backflip tutorial room through
# the door, and the half-run state then divided by a zero element count in
# sub_00199710 (HeroLab task 4f1a2649).
# 17 data-only entry points (2026-10-04), from a scan for immediates (mov/push)
# in lifted code that name an unlifted .text address preceded by int3/ret
# padding (scan_imm_function_refs.py). Nine are real function starts:
# 0x00020E30 0x00026240 0x000412B0 0x000C6330 0x001135D0 0x001251D0
# 0x001315B0 0x00134F10 0x00171C90. 0x001251D0 is stored at 0x00125DEE and
# failed right after the first save wrote data.sav (the "saving" banner then
# never cleared); 0x001135D0 (stored at 0x00113EE2) failed later in bettou.
# The other eight are compiler stubs: C++ EH funclets 0x001BD4E3/518/543/55B
# (mov eax, table; jmp handler) and atexit destructors 0x001BD7D0/7E0/7F0/800
# (mov ecx, object; jmp dtor). Four more hits point into int3 padding (data,
# not code) and are left out.
SEEDS = [
    0x0018C880, 0x001D1F04, 0x001D1E4F, 0x001D1E9D, 0x001D1F9C, 0x001D1F51,
    0x001D3A11, 0x001D1379, 0x001AF8E5, 0x001AF8ED, 0x001D5E00, 0x001D62DF,
    0x001D63E5, 0x001D7E73, 0x001D8A14, 0x001B1594, 0x001D36C0, 0x001D2460,
    0x0017F320, 0x001A8460, 0x001A7F72, 0x001B6603, 0x001B5F75, 0x001AF8F5,
    0x001D5B6A, 0x001D136C, 0x001D1636, 0x001D88AB, 0x001BD660, 0x001BD670,
    0x001BD680, 0x001BD7C0, 0x0001FD40, 0x00033130, 0x000AF960, 0x00026760,
    0x001D1E08, 0x001D558F, 0x001AF92E, 0x00204907, 0x00206456, 0x001D5DB5,
    0x001DBD74, 0x001B6849, 0x0020A719, 0x001DAB69, 0x001D4F3F, 0x001D4D5F,
    0x001D5A85, 0x001D8397, 0x0001B900, 0x00113630, 0x00129240, 0x00113110,
    0x0011C4C0, 0x00117870, 0x00115650, 0x00096CE0, 0x00024CB0, 0x000F5860,
    0x0007DC60, 0x000AE2C0, 0x000AE6C0, 0x000CFB01, 0x000CFB0F, 0x000CFB14,
    0x000CF895, 0x000CF8C3, 0x000CF8F8, 0x000CF943, 0x000CF9C5, 0x000CFA53,
    0x00020E30, 0x00026240, 0x000412B0, 0x000C6330, 0x001135D0, 0x001251D0,
    0x001315B0, 0x00134F10, 0x00171C90, 0x001BD4E3, 0x001BD518, 0x001BD543,
    0x001BD55B, 0x001BD7D0, 0x001BD7E0, 0x001BD7F0, 0x001BD800, 0x000D7894,
    0x000D7DF0, 0x000F6A60, 0x000F6F20, 0x00100373, 0x001B0B0E, 0x001B32EE,
    0x001B3C54, 0x001B5F9F, 0x001BD439, 0x001BD4A2, 0x0016FC60, 0x001674B0,
]


# Runs `python -m <module>` with the toolkit on sys.path, written out because
# `-m` alone resolves modules against the working directory, and the embedded
# Python the Windows launcher ships ignores that (its ._pth file fixes
# sys.path, as Python's embeddable distribution is meant to).
_RUN_MODULE = ("import runpy, sys; sys.path.insert(0, sys.argv[1]); "
               "sys.argv = [sys.argv[2]] + sys.argv[3:]; "
               "runpy.run_module(sys.argv[0], run_name='__main__', alter_sys=True)")


def module_cmd(toolkit, module, *args):
    return [sys.executable, "-c", _RUN_MODULE, toolkit, module, *args]


def step(title, cmd, toolkit):
    print(f"==> {title}", flush=True)
    subprocess.run(cmd, cwd=toolkit, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--xbe", default=os.path.join(PROJECT, "game_files", "default.xbe"))
    ap.add_argument("--toolkit", default=os.environ.get("XBOXRECOMP", "/mnt/dev/xboxrecomp-fork"))
    args = ap.parse_args()
    xbe = os.path.abspath(args.xbe)
    toolkit = os.path.abspath(args.toolkit)
    gen = os.path.join(PROJECT, "src", "recomp", "gen")
    if not os.path.isfile(xbe):
        sys.exit(f"missing {xbe} -- supply your own disc image")
    if not os.path.isdir(os.path.join(toolkit, "tools")):
        sys.exit(f"missing the xboxrecomp toolkit at {toolkit}")

    def mod(module, *rest):
        return module_cmd(toolkit, module, *rest)

    fd, seeds = tempfile.mkstemp(prefix="breakdown-seed.", suffix=".json")
    with os.fdopen(fd, "w") as f:
        json.dump([{"start": f"0x{a:08X}"} for a in SEEDS], f)
    try:
        # NOT --text-only: Breakdown's NuSoundStream (internal audio-streaming
        # subsystem) has real code in the DSOUND section, which --text-only skips
        # entirely. Without it, NuSoundStream's worker thread spins forever polling
        # a completion flag nothing can ever set (HeroLab task 999ce5ca). Dropping
        # --text-only pulls in D3D/D3DX/XGRPH/DSOUND/XMV/XPP/DOLBY together; only
        # DSOUND is named explicitly below because that's the one confirmed to
        # contain reachable code so far -- if a future investigation needs another
        # of those sections specifically, add it here too rather than assuming this
        # one covers everything.
        step("disassembling", mod("tools.disasm", xbe, "--extra-sections", "DSOUND",
                                  "--seed-functions", seeds, "--force", "-v"), toolkit)
        step("identifying library functions", mod("tools.func_id", xbe, "-v"), toolkit)
        step("analysing calling conventions", mod("tools.abi_analysis", xbe, "-v"), toolkit)
        shutil.rmtree(gen, ignore_errors=True)
        step("lifting to C", mod("tools.recomp", xbe, "--all", "--split", "1000",
                                 "--gen-dir", gen,
                                 "--exclude-manual", os.path.join(PROJECT, "src", "recomp_manual.c")),
             toolkit)
    finally:
        os.remove(seeds)
    print(f"==> done; now rebuild in {os.path.join(PROJECT, 'build-win')}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
