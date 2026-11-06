# Breakdown native PC — roadmap

Unofficial fan / preservation project. Own the game. No dumps in git.

## Priority order

### P0 — Stability
1. **Fix flickering textures** (likely lighting + texture / occlusion interaction). Playable image first.
   Notes: [docs/flicker-investigation.md](docs/flicker-investigation.md). The exe defaults occlusion queries to `sync` (lights and overlays). Mip and render-target defaults were left alone.

### P1 — Display
Player-facing **Options → Video** (and an accessibility section in that same priority). Same kind of in-game control a modern PC FPS ships: the menu is required, and env vars / `run_native.cmd` stay the host path underneath it. Accessibility is a P1 must-have, not a later pass.

2. **Modern aspect ratios** — 16:9, 16:10, ultrawide where feasible, with HUD-safe framing.
   - [x] Letterbox the 4:3 frame in a 16:9, 16:10, or 21:9 window (`RECOMP_DISPLAY_ASPECT`). HUD stays in the 4:3 image. [docs/widescreen-hires.md](docs/widescreen-hires.md).
   - [ ] Hor+ (a wider camera). Pin `0692070` logs any `display.aspect` other than 4:3 and keeps 4:3. `nv2a_host_opts` has no aspect or FOV field, so linking the enhancements layer does not widen the view. A later `blinx2/portability` commit (`7a97fcd`) implements Hor+ with the HUD kept in the 4:3 columns; this repo stays on the pin the window and input patches match. The video menu does not offer Hor+. Letterbox remains the wide-window mode. [docs/widescreen-hires.md](docs/widescreen-hires.md).
3. **Higher internal / output resolutions** beyond the Xbox default.
   - [x] `RECOMP_RENDER_SCALE` 1..4 and `RECOMP_WINDOW_SCALE` 1..6. `run_native.cmd` uses 2 and 16:9.
   - [x] Exclusive fullscreen, besides borderless. `RECOMP_PRESENT_FULLSCREEN=1` stays borderless. `exclusive` or `2` calls `SetFullscreenState` at the window size (`RECOMP_WINDOW_SCALE` × `RECOMP_DISPLAY_ASPECT`) after `patch_d3d11_exclusive.py`. The guest frame stays 4:3 and is letterboxed in that mode. If the driver refuses, the log says so and the window stays at that client size. VSync and further output modes are still open under item 4.
4. **In-game Video / Graphics menu** — full in-game control, not env-only. Required so weaker PCs can choose performance against clarity. The menu persists choices and applies them through the existing `RECOMP_*` host knobs (launch scripts keep working; a normal launch reads the saved config when those vars are unset). Items below are the parity goals. They are not all implemented. This slice ships the Video rows and the config file ([docs/video-options.md](docs/video-options.md)). Accessibility, presets, VSync, and hor+ are still open. Exclusive fullscreen is in (item 3).
   - **Display**
     - [x] Resolution and window mode: windowed, borderless, and exclusive, from the Video menu (pause / Start, with F10 as the backup hotkey) and from `breakdown_video.cfg` beside the exe. Sizes are `RECOMP_WINDOW_SCALE` 1..6 × `RECOMP_DISPLAY_ASPECT`. Exclusive uses that size as the display mode. The 4:3 frame stays letterboxed. Hor+ is still item 2 and is not offered.
     - [x] Aspect: letterbox `4:3`, `16:9`, `16:10`, or `21:9`. Hor+ is not offered (item 2).
     - [ ] VSync and a frame cap, once the present path can support them. `RECOMP_PRESENT_PACING` only changes how lowered spin-waits sleep. It is not this option.
     - [x] Present filter (`RECOMP_PRESENT_FILTER`): nearest, linear, or integer, from the same menu. A wide window at render scale 1 with an explicit nearest filter still copies to the top left; the menu says so. Presets that would force linear or integer at that combination are the next slice.
   - **Graphics quality**
     - [x] Render scale / internal resolution (`RECOMP_RENDER_SCALE` 1..4), from the same menu. This is the main cost lever (targets and the present texture grow with the scale). A change is requested for the next frame (`nv2a_host_opts_request`); it does not need a new process.
     - [x] Texture and filter quality where the stack allows a safe player control. The present filter above is that control. In-scene mip and point-sample switches (`RECOMP_D3D11_NO_MIPS`, `d3d11_point`) stay off the menu until they are shown not to bring flicker back. See [docs/flicker-investigation.md](docs/flicker-investigation.md).
     - [ ] Effects toggles that change performance on weaker PCs, only for an effect the host can cheapen without a new art or post-process pipeline. Do not put flicker investigation switches (`RECOMP_D3D11_OCC`, `RECOMP_D3D11_NO_RTT`) in this menu.
   - **Persistence and presets**
     - [x] Save the menu to `breakdown_video.cfg` beside the exe and apply it on the next launch through the `RECOMP_*` host path. A set variable wins over the file. An unset variable uses the file. Neither stays at the stock defaults. `run_native.cmd` still sets its variables, so that launch keeps winning until those lines are removed.
     - [ ] Ship a low-end preset and a high-end preset inside the same clamps (render scale 1..4, window scale 1..6, the filters above). Low-end favours a smaller internal resolution and a cheap window; high-end favours a higher render scale and a sharper present. `run_native.cmd` can keep its current 2× / 16:9 launch.
5. **Accessibility** — first-class P1, with the video menu. Not deferred to P2, P3, or the parallel tracks.
   - [ ] HUD / UI scale or readability inside the 4:3 frame. The HUD is drawn into the same colour target as the scene, so it scales with render scale and the letterbox does not move it. The menu exposes a readability control on that frame (internal scale and window size). A HUD that leaves the 4:3 image is hor+ (item 2), not this checkbox.
   - [ ] Contrast and colorblind-friendly cues where they can be done without rewriting the art pipeline (present-time or a simple overlay). Replacement art stays P2.
   - [ ] Subtitles / captions when the game has dialogue or spoken lines that can be surfaced. This does not add a new script. If a line exists only as audio with nothing to show, record that limit here instead of inventing a track.
   - [ ] Control remapping. Full remapping stays on the parallel controls track below (in-game remap UI is out of scope for the current mouse/keyboard track; see [docs/mk-controls-design.md](docs/mk-controls-design.md)). Accessibility in this item does not wait on that track. Shipped mouse/keyboard defaults stay reachable until the remapper exists, and the remapper has to cover this need when it lands.
   - [ ] Comfort options: motion and flicker toggles once a real control exists (after the P0 flicker fix, or a host option that actually changes comfort). This pin has no motion-blur or camera-shake switch to expose.

### P2 — Presentation
6. **Anniversary-style graphics toggle** — switch between original look and touched-up assets (like Halo CE Anniversary original/remastered). Default: original. Updated pack optional / generated locally from your dump where possible.

### P3 — Speedrun / competitive
7. **Built-in timer + splits/stats** for categories such as Burger, 100%, Any%, etc.
8. **In-game leaderboard** for players who opt in to post times.
9. Hook into a future **multiplayer / competitive** mod where it fits.

### Parallel / next after P0–P1
- Mouse/keyboard (Modern punches LT/RT; auto-apply patches) — in progress
- Full remapping + Legacy vs Modern control schemes (keep original melee combos)
- Character skin swaps in Extras
- Platform notes: Linux/SteamOS, then Android/iOS/web feasibility

## Working rule
If a decision needs the owner and they don’t answer in ~5 minutes, pick the best recommendation and keep shipping.