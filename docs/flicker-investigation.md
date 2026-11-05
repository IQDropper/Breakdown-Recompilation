# Texture flicker — investigation

Date: 2026-11-05  
Play report: textures flicker. Suspected cause: lighting interacting with textures.  
`run_native.cmd` already set `RECOMP_D3D11_OCC=sync` before this note.

This pass did not replay the game. There is no disc dump in git, and this change was not run on a Windows GPU. Findings below are from the repo’s launch scripts and `patch_*.py`, checked against the pinned toolkit (`game.toml` `[toolkit] commit` `0692070cb41d53d225711d8a8183efce0a388019`, `apexJCL/xboxrecomp` branch `blinx2/portability`). `external/` is gitignored and was not committed.

## What was searched

- Launchers: `run_native.cmd`, `run_native_occdefer.cmd`, `run_native_nortt.cmd`, `run_native_cpu.cmd`, `keys.ps1`, `to_load_screen.ps1`, `bench.ps1`
- Every `patch_*.py` docstring and the D3D11 sites they edit (`patch_zonly.py`, `patch_vs_strict.py`, `patch_rtt_sub.py`, `patch_fb_window.py`, `patch_skip_why.py`, `patch_d3d_callback.py`)
- Pinned toolkit: `src/d3d/nv2a_pb_d3d11.c`, `src/kernel/nv2a_backend_common.c`, `src/kernel/nv2a_backend_common.h`, `src/platform/recomp_env.h`, `src/platform/recomp_env.c`

## 1. Occlusion queries — confirmed light / overlay flicker

Visibility tests are D3D11 occlusion queries (`nv2a_pb_d3d11.c`, “Visibility tests”). `GET_REPORT` does not finish on the GPU immediately.

- Toolkit default (`RECOMP_D3D11_OCC` unset): the report status is set to `0xFFFFFFFF` (`D3DERR_TESTINCOMPLETE`) and the result is polled later. A report still pending after `NV2A_OCC_LATE_US` (250 ms), or one that overflows, is completed as **visible** (`NV2A_OCC_VISIBLE` = `0x10000`), not as occluded. See `nv2a_occ_report` / `nv2a_occ_poll` in `nv2a_backend_common.c`.
- `RECOMP_D3D11_OCC=sync`: `GET_REPORT` waits up to that same 250 ms, then polls. `run_native.cmd` already did this. Its comment is from play: deferred answers made lights and overlays flicker on and off.
- `RECOMP_D3D11_OCC=fixed`: every test is left to the pushbuffer walker as visible (`RECOMP_ZPASS_FIXED`). That is not a flicker fix.
- Any other value (`defer`, `0`, …) is deferred, same as unset. `occ_mode()` only special-cases `sync` and `fixed`.

That matches the report’s “lighting + textures” guess for **lights and the overlays drawn from them**. It does not, by itself, explain a surface whose albedo swaps every frame while the light stays on.

### Gap this change closes

Only `run_native.cmd` set `sync`. README and `SETUP.cmd` also tell you to run `build-win\breakdown_recomp.exe` directly. `keys.ps1` / `to_load_screen.ps1 -Backend d3d11` did not set it either. Those launches stayed on the deferred path that the play script already rejected.

The toolkit’s game-default hook (`RECOMP_ENV_GAME_DEFAULTS` in `recomp_env.c`, used the same way as BLiNX 2) now sets `D3D11_OCC` to `sync` when neither `RECOMP_D3D11_OCC` nor `RECOMP_DEBUG=d3d11_occ=...` is set. Wired in `CMakeLists.txt` via `src/env/recomp_env_game.h`. An explicit `defer` or `fixed` still wins. **Rebuild** (`breakdown all` or `breakdown build`) or the old exe keeps the toolkit default.

`run_native_occdefer.cmd` now sets `RECOMP_D3D11_OCC=defer` on purpose. Leaving the variable unset is no longer the deferred A/B.

### What sync does not fix

If the wait hits 250 ms, the same poll still completes the report as visible. On a loaded GPU (the README’s GTX 1660 Ti is the playtest card) an occluded light can still pop on. The flip log already counts this. Do not change the fallback until a log shows `late` climbing while the image flickers. Forcing “hidden” on timeout would turn real lights off whenever the GPU is merely slow.

## 2. Render targets and textures

D3D11 never writes the guest copy of a colour surface. A texture is uploaded by hashing guest bytes **once per flip** (`tex_bind`). A stage whose texture starts at a render target’s address is bound to that GPU texture instead (`rt_texture`), but only when:

- the low 27 bits of the address match exactly
- texel size matches (32-bit, or the 16-bit formats `rt_16bit` lists)
- a linear texture’s pitch matches the surface

Anything else decodes guest memory. For a surface this backend drew, those bytes are stale or empty.

`patch_rtt_sub.py` extends the match to a linear texture that starts **inside** the surface (same pitch; the comment’s example is the menu overlay 16 rows down). Its own note says the miss painted **transparent black**, not a flicker. The patch still matches the pinned `nv2a_pb_d3d11.c` (each anchor occurs once). `SETUP.cmd` does not run it. Apply it only when an overlay or screen effect is black, then rebuild the toolkit.

`RECOMP_D3D11_NO_RTT=1` skips that bind and always decodes guest memory. `run_native_nortt.cmd` is that A/B. It previously also left occlusion deferred, so a “textures still flicker” result mixed the known light bug back in. It now sets `RECOMP_D3D11_OCC=sync`.

Stale render targets: `nv2a_rt_own_stale` drops a target whose guest bytes changed since it was created, unless it was drawn this flip. The comment in `nv2a_pb_d3d11.c` is the menu leaving 512×512 targets whose memory the next level refills with textures. `RECOMP_DEBUG=rt_alias_check=0` keeps the old GPU image in that case. That can replace a flicker with the wrong texture stuck on screen. Not a default.

## 3. Mips and filtering

Swizzled and DXT textures upload every level `SET_TEXTURE_FORMAT` counts (clamped like xemu). Linear textures are one level. The sampler maps NV2A min/mag/mip onto a `D3D11_FILTER` (`nv2a_tex_filter_linear` / `nv2a_tex_filter_mip` in `nv2a_backend_common.h`): nearest mip is `MIP_POINT`, blended mips are `MIP_LINEAR`, and min/mag linear set the matching bits. LOD bias is the signed 5.8 field, clamped to ±16.

`RECOMP_D3D11_NO_MIPS=1` uploads level 0 only. The backend comment calls it an A/B, and nothing in this tree says Breakdown’s flicker is a bad mip chain. A chain that shimmers as the camera moves is the case to try it. A pop in place is not. `RECOMP_DEBUG=d3d11_point` forces nearest texels. Neither is a default.

`patch_fb_window.py` sets GDI `HALFTONE` on the CPU framebuffer window. `run_native.cmd` uses `RECOMP_PB_BACKEND=d3d11` and must **not** set `RECOMP_FB_WINDOW` (that window shows guest memory D3D11 never writes, so it is blank). HALFTONE is not the D3D11 texture path.

## 4. Depth pre-pass — black rooms, not flicker

`patch_zonly.py`: Xbox `SetRenderTarget(NULL, zbuffer)` is a depth-only pass. D3D11 skipped those draws and their depth clears, so the later depth-EQUAL colour pass drew nothing. Rooms were black on D3D11 and fine on the CPU backend.

`patch_vs_strict.py`: the colour pass uses depth EQUAL, which needs bit-identical position. It compiles vertex shaders with `D3DCOMPILE_IEEE_STRICTNESS` (xemu’s `invariant gl_Position`). Without it the room is black, not flickering.

Both patches still match the pinned file (each anchor once). They are not applied by `SETUP.cmd`. Run them when rooms are black, then rebuild. They were not applied here: the reported bug is flicker, and these comments describe a missing image.

## 5. One upload per flip

`tex_bind` rehashes a cached texture only when the flip generation changes. A texture rewritten between two batches of the same flip stays at the first upload (the file’s own “ponytail” note). That is a one-frame lag, or a flicker if the game ping-pongs bytes in one allocation and the hash races the write. Fixing it means hashing on every bind inside the toolkit. No env switch does that, and it was not changed without a capture showing `changed` / `rehash` on the flip log while a static surface flickers.

## Change made

| Item | Why |
|---|---|
| `src/env/recomp_env_game.h` + `CMakeLists.txt` | Unset `RECOMP_D3D11_OCC` becomes `sync`, including a direct exe launch. Explicit `defer` / `fixed` still win. |
| `run_native_occdefer.cmd` | Sets `defer`, so the A/B still exists after the default. |
| `run_native_nortt.cmd` | Keeps `sync` while `NO_RTT=1`, so the texture A/B is not also the light A/B. |
| `run_native.cmd` | Still sets `sync`. Comment points here. |

Not changed, because the evidence does not support them as defaults: `RECOMP_D3D11_NO_MIPS`, `d3d11_point`, `rt_alias_check=0`, auto-applying `patch_rtt_sub.py` / `patch_zonly.py` / `patch_vs_strict.py`, and the 250 ms “complete as visible” fallback.

## Next playtest (needs your dump and a rebuild)

1. Rebuild so `recomp_env` picks up the header. Confirm stderr is not required for the default; it is compiled in.
2. `set RECOMP_FLIP_LOG=1` then `run_native.cmd`. On a flickering stretch, read `[D3D11] flip`:
   - `occ (visible … late … sync … us)` — `late` non-zero means the 250 ms visible fallback fired.
   - `rtt` / `self` — render-target textures and copies while drawing into the same surface.
   - `uploads (mip) changed rehash` — textures moving every frame.
3. The present summary every 300 frames includes `stale drops`. A climb there is the alias check throwing GPU images away.
4. A/B, one variable each:
   - `run_native_occdefer.cmd` — deferred queries (the old light flicker).
   - `run_native_nortt.cmd` — no render-to-texture, occlusion still sync.
   - one session with `RECOMP_D3D11_NO_MIPS=1` only if the shimmer follows camera distance.
5. Black overlay or screen effect, not flicker: `py -3 patch_rtt_sub.py` and rebuild.
6. Black rooms: `py -3 patch_zonly.py` and `py -3 patch_vs_strict.py`, then rebuild.

## Checks done here

- `patch_zonly.py`, `patch_vs_strict.py`, `patch_rtt_sub.py`, and `patch_skip_why.py` anchors each occur once in the pinned `nv2a_pb_d3d11.c`.
- `RECOMP_ENV_GAME_DEFAULTS(D) D(D3D11_OCC, "sync")` expands to the assignment `recomp_env.c` already uses (`if (!set[RENV_D3D11_OCC]) values[RENV_D3D11_OCC] = "sync"`). A small host program compiled and ran that expansion: unset becomes `sync`; a set value is left alone.
- The game itself was not built or launched.
