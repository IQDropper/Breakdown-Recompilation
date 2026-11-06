# Widescreen and higher resolution

Date: 2026-11-05  
Toolkit pin: `0692070cb41d53d225711d8a8183efce0a388019` (`game.toml`, `blinx2/portability`).  
P2 (Anniversary original/updated art) is not this change. No replacement textures are added.

This pass did not launch the game. There is no disc dump in git, so the title’s `SetDisplayMode` call was not read from the XBE. The notes below are the host window, the present path, and the enhancements layer at that pin.

## What the Xbox title is treated as

The D3D11 backend creates its window at **640×480**. That is 4:3, and it is the stock client for every title on this backend, not a mode parsed from Breakdown’s executable. The CPU framebuffer window was the same size before `patch_fb_window.py` (`RECOMP_WINDOW_SCALE`, default 2 on that path only).

The frame that is shown is the surface the title flips. Its width and height come from the guest, not from the window. Render targets are created at that guest size (times `render.scale`). Comments in the backend describe a 640×480 back buffer drawn with smaller clips in the same frame. That matches a 4:3 Xbox back buffer. It is not proof that Breakdown never selects another mode; confirming that needs a dump and a log of the present extent (`RECOMP_FLIP_LOG=1`, the present line’s surface size, or the `[DISPLAY]` window line next to `[D3D11] present`).

The HUD is drawn by the game into that same colour target. Nothing in this tree moves HUD elements independently of the scene. Scaling the target scales the HUD with it. Putting the 4:3 image in a wider window with bars does not reposition the HUD. A wider camera would.

## What this toolkit pin already does

`src/enhance/enhance.c` (`xbox_enhance_init`, only when the build sets `XBOXRECOMP_ENHANCE=ON`) copies config / env into `nv2a_host_opts`:

| Key | Env (only if the enhance keys are compiled in) | Behaviour at this pin |
|---|---|---|
| `render.scale` | `RECOMP_RENDER_SCALE` | Integer internal scale **1..4**. Targets and the present texture are `guest × N`. Occlusion counts are divided by `N²`. |
| `present.filter` | `RECOMP_PRESENT_FILTER` | `nearest` (stock), `linear`, or `integer`. Fit keeps the frame’s aspect, centred. `integer` is the largest whole multiple that fits. |
| `present.fullscreen` | `RECOMP_PRESENT_FULLSCREEN` | In the enhance layer, a boolean: borderless desktop fullscreen. The frame is still letterboxed by the blit. This repo also accepts `exclusive` (see below). The layer does not. |
| `display.aspect` | `RECOMP_DISPLAY_ASPECT` | **Only `4:3` is implemented.** Any other value is logged and ignored: “hor+ is a later slice”. The string is not stored on `nv2a_host_opts`. |
| `present.pacing` | `RECOMP_PRESENT_PACING` | How lowered spin-waits sleep. Not a display mode. |

This repo’s `game.toml` leaves `stock_cmake` empty, so the enhancements library is **not** linked and nothing calls `xbox_enhance_init`. Without another caller, those options stay zero: guest-size targets, nearest, windowed.

Present (`nv2a_present_rect`):

- **Letterbox** (nearest / linear / integer): the largest rectangle of the **frame’s** aspect that fits the window, centred. Bars are cleared black.
- **No stretch mode.** A stock present (scale 1, nearest, not fullscreen, swap chain still the size it was created at) copies the frame. If that window is wider than the frame, the copy sits at the **top left**, not in the centre. Scale other than 1, or a filter other than nearest, takes the letterbox blit instead.
- Resizing the window (`WM_SIZE`) resizes the swap chain. The same letterbox rules apply on the next present once the blit path is active.

`RECOMP_WINDOW_SCALE` is a real config key. The D3D11 window did **not** read it. Only the CPU framebuffer patch and the SDL host do. `RECOMP_FB_WINDOW` must stay off for a D3D11 run: that window shows guest memory the backend never writes.

Hor+ (render extra horizontal FOV, then pin the HUD) is the unimplemented slice named in `enhance.c`. It is not a window-size change.

## Hor+ is blocked on this pin

Checked against `src/enhance/enhance.c` and `struct nv2a_host_opts` at `0692070`:

- `xbox_enhance_init` reads `display.aspect`. If it is not `4:3`, it logs `hor+ is a later slice` and forces `4:3`. Nothing copies the ratio into `nv2a_host_opts`.
- That struct is `render_scale`, `present_filter`, `fullscreen`, and `pack_off`. There is no aspect, no horizontal pad, and no FOV scale.
- Present (`nv2a_present_rect`) letterboxes the guest frame into the window. A wider window does not widen the guest target or the projection.
- `game.toml` leaves `stock_cmake` empty, so `XBOXRECOMP_ENHANCE` stays off. Turning it on would still force 4:3. This repo does not link the layer for Hor+.
- The guest projection is the title’s. There is no dump in git, and this repo does not patch it. A present-time stretch would be anamorphic, not extra horizontal FOV, and it would distort the HUD. It is not added.

A later commit on `blinx2/portability`, `7a97fcd` (changelog for blinx2-v0.14.0), does implement Hor+: `display.aspect` pads 4:3 targets, 2D batches stay in the 4:3 columns, and the 3D view is wider. That commit is not this pin. The window, input, and flicker patches in this repo match `0692070`. Moving the pin is a toolkit upgrade, not a display-only change, so it is not done here. The Video menu does not offer Hor+.

## What Breakdown still needs

- Read the real display mode from a dump (present extent in the flip log) and record it here if it is not 640×480.
- Hor+: a wider view, with the HUD anchored to the 4:3 safe area. Blocked on this pin, as above. It needs the later toolkit’s Hor+ (or a pin move), not another window size.
- A stretch-to-fill option. Not added. It would distort the HUD. Letterbox is the HUD-safe framing.
- Output modes other than the window sizes below, borderless, and exclusive. VSync is still not a menu option (`RECOMP_PRESENT_VSYNC` exists on the backend; the menu does not expose it).
- P2 art (original vs updated). Out of scope.

## What this change adds

`src/display_host.c` reads these with `getenv` (they do not depend on the enhancements layer) and applies them before the guest starts:

| Variable | Default if unset | `run_native.cmd` |
|---|---|---|
| `RECOMP_RENDER_SCALE` | `1` (clamped to 1..4) | `2` |
| `RECOMP_WINDOW_SCALE` | `1` (clamped to 1..6) | `2` |
| `RECOMP_DISPLAY_ASPECT` | `4:3` | `16:9` |
| `RECOMP_PRESENT_FILTER` | `nearest` | `nearest` |
| `RECOMP_PRESENT_FULLSCREEN` | windowed | unset (windowed) |

`RECOMP_PRESENT_FULLSCREEN` values:

| Value | Mode |
|---|---|
| unset, `0`, `false`, `no`, `windowed`, anything else | Windowed at the client size below |
| `1`, `true`, `yes`, `borderless` | Borderless popup over the monitor. The client size is not the swap chain. |
| `exclusive`, `2` | Exclusive: `SetFullscreenState` at the client size below. Needs `patch_d3d11_exclusive.py`. |

Window client, before borderless replaces it with the monitor. Exclusive uses this size as the display mode:

- `4:3`: `640×scale` by `480×scale`
- `16:9`, `16:10`, `21:9`: height `480×scale`, width that aspect, rounded to the nearest pixel

Examples at window scale 2: 4:3 is 1280×960, 16:9 is 1707×960, 16:10 is 1536×960, 21:9 is 2240×960.

`RECOMP_DISPLAY_ASPECT` does **not** change the camera. The 4:3 frame is letterboxed. Unknown aspect strings fall back to 4:3.

If the aspect is not 4:3, scale is 1, and the filter was left unset, the filter is promoted to `linear` so the present takes the centred blit instead of the top-left stock copy. An explicit `nearest` is left alone (top-left at scale 1). `run_native.cmd` uses scale 2, which already leaves the stock copy, so nearest stays sharp and the wide window still letterboxes.

`patch_d3d11_window.py` (via `APPLY_DISPLAY_PATCHES.cmd` from `SETUP.cmd`) adds an optional hook in the D3D11 backend, `breakdown_display_window_size_hook`. `display_host_init` sets it before the guest starts. If the pointer is NULL the window stays 640×480. Toolkit smoke tests (`d3d11_backend_smoke` and the rest of `xbox_d3d8`) do not link the game, so they keep that default and still link. An earlier revision of the patch called a hard extern and those targets failed with `undefined symbol: breakdown_display_window_size`. Re-run `APPLY_DISPLAY_PATCHES.cmd` to upgrade that call.

`patch_d3d11_exclusive.py` is the same script’s second step. It defines `breakdown_display_set_exclusive`. Fullscreen `2` keeps the hooked client size, creates the swap chain with `DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH`, and calls `SetFullscreenState`. Fullscreen `1` is still the borderless monitor popup. A resize leaves exclusive first (`ResizeBuffers` fails while it is held) and the next flip enters again when the option is still `2`. Alt+Enter is turned off so DXGI does not switch mode under the Video menu. If `SetFullscreenState` fails, stderr says so and the window stays at that client size. If the symbol is not linked, startup uses borderless instead of handing `2` to the unpatched backend (any non-zero value there is borderless).

Until the window patch is applied and both the toolkit and the exe are rebuilt, the window still opens at 640×480. Scale, filter, and borderless still apply, and you can drag the window larger. Dragging is the same letterbox once the blit path is on. Exclusive without its patch is borderless.

`breakdown build` does not reconfigure when `build-win/CMakeCache.txt` already exists. A tree configured before `src/display_host.c` was on the exe will not compile that file, and the old hard extern then fails the link. The patch script deletes that cache when `build-win/build.ninja` does not mention `display_host.c`. The next build configures again. Object files are left in place. If cmake objects, delete `build-win` and configure from scratch. `CMakeLists.txt` also fails configure if the exe target loses `src/display_host.c`.

stderr (also `run_stderr.txt`):

```text
[DISPLAY] render.scale=2 present.filter=nearest fullscreen=0 (windowed) window=1707x960 aspect=16:9 (letterbox, 4:3 frame; hor+ is not in this pin)
```

The window numbers are the windowed client, and the exclusive mode size. Borderless ignores them for the swap chain and letterboxes onto the monitor. Exclusive asks the display to switch to that size; the 4:3 frame is still letterboxed inside it. `fullscreen=2 (exclusive)` is that mode. A following `[d3d11] exclusive fullscreen on` means `SetFullscreenState` succeeded. `exclusive fullscreen failed` means the driver refused and the window was left at the client size.

Direct `build-win\breakdown_recomp.exe` with a clean environment and no `breakdown_video.cfg` beside the exe stays stock: scale 1, 640×480, nearest. If that file exists, its values fill any `RECOMP_*` variable that is unset. A variable that is set still wins, including the ones `run_native.cmd` sets. The in-game menu that writes the file is [video-options.md](video-options.md) (pause / Start, or F10).

## Verify on Windows (your dump)

1. `SETUP.cmd` (or `APPLY_DISPLAY_PATCHES.cmd` if `external\` is already there). The window script prints `patched d3d11 window size`, `upgraded hard extern to optional hook`, or `already patched`. The exclusive script prints `patched d3d11 exclusive fullscreen` or `already patched`. If the window script removed `build-win/CMakeCache.txt`, the next build reconfigures so `display_host.c` is on the exe.
2. `breakdown all` (rebuild the toolkit and the exe after the patch).
3. `run_native.cmd`.
4. In `run_stderr.txt`:
   - `[DISPLAY] ... window=1707x960 aspect=16:9`
   - The D3D11 device line should show a swap chain of that size (not 640×480), unless fullscreen was set.
5. The picture should be the 4:3 game with black bars on the sides, HUD the same shape as in a 4:3 window, sharper than scale 1.
6. Edit `run_native.cmd` and launch again (no rebuild):
   - `RECOMP_DISPLAY_ASPECT=16:10` → window `1536x960`
   - `RECOMP_DISPLAY_ASPECT=4:3` → `1280x960`, no side bars
   - `RECOMP_RENDER_SCALE=1` → softer, Xbox-sized targets
   - `RECOMP_PRESENT_FULLSCREEN=1` → borderless, 4:3 image centred on the monitor
   - `RECOMP_PRESENT_FULLSCREEN=exclusive` → `[DISPLAY] ... fullscreen=2 (exclusive)` and `[d3d11] exclusive fullscreen on`. The desktop mode should become the window size (1707×960 at this scale and aspect). The picture stays a letterboxed 4:3 frame, not a wider camera. Alt+Tab should return; the next flip leaves exclusive if you set the variable back to `0`.
7. Optional: `set RECOMP_FLIP_LOG=1` and confirm the present extent. If it is not 640×480, the “Xbox back buffer” note above needs an update. Hor+ is still not implemented either way.
8. In-game Video (pause / Start, or F10) and `breakdown_video.cfg` are [video-options.md](video-options.md). `run_native.cmd` still overrides that file for any variable it sets.
