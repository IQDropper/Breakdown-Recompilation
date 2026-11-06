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
   - [ ] Hor+ (a wider camera). This toolkit pin logs any `display.aspect` other than 4:3 and keeps 4:3. Still its own item: the video menu offers letterboxed aspects, and offers hor+ only once this pin implements it. HUD readability does not move the HUD out of that frame.
3. **Higher internal / output resolutions** beyond the Xbox default.
   - [x] `RECOMP_RENDER_SCALE` 1..4 and `RECOMP_WINDOW_SCALE` 1..6. `run_native.cmd` uses 2 and 16:9.
   - [ ] Modes other than borderless fullscreen (`RECOMP_PRESENT_FULLSCREEN=1` is the borderless switch). Exclusive fullscreen is not on this present path yet.
4. **In-game Video / Graphics menu** — full in-game control, not env-only. Required so weaker PCs can choose performance against clarity. The menu persists choices and applies them through the existing `RECOMP_*` host knobs (launch scripts keep working; a normal launch reads the saved config when those vars are unset). Items below are the parity goals. They are not all implemented. The menu, the config file, Low/High presets, and HUD readability are in [docs/video-options.md](docs/video-options.md). Contrast, subtitles, comfort, remapping, VSync, exclusive fullscreen, and hor+ are still open.
   - **Display**
     - [x] Resolution and window mode: windowed and borderless, from the Video menu (F10) and from `breakdown_video.cfg` beside the exe. Sizes are `RECOMP_WINDOW_SCALE` 1..6 × `RECOMP_DISPLAY_ASPECT`. The menu calls the second mode borderless. Exclusive fullscreen is still item 3 and is not offered.
     - [x] Aspect: letterbox `4:3`, `16:9`, `16:10`, or `21:9`. Hor+ is not offered (item 2).
     - [ ] VSync and a frame cap, once the present path can support them. `RECOMP_PRESENT_PACING` only changes how lowered spin-waits sleep. It is not this option.
     - [x] Present filter (`RECOMP_PRESENT_FILTER`): nearest, linear, or integer, from the same menu. A wide window at render scale 1 with an explicit nearest filter still copies to the top left; the menu says so. Low uses linear and High uses integer, so neither preset is that combination.
   - **Graphics quality**
     - [x] Render scale / internal resolution (`RECOMP_RENDER_SCALE` 1..4), from the same menu. This is the main cost lever (targets and the present texture grow with the scale). A change is requested for the next frame (`nv2a_host_opts_request`); it does not need a new process.
     - [x] Texture and filter quality where the stack allows a safe player control. The present filter above is that control. In-scene mip and point-sample switches (`RECOMP_D3D11_NO_MIPS`, `d3d11_point`) stay off the menu until they are shown not to bring flicker back. See [docs/flicker-investigation.md](docs/flicker-investigation.md).
     - [ ] Effects toggles that change performance on weaker PCs, only for an effect the host can cheapen without a new art or post-process pipeline. Do not put flicker investigation switches (`RECOMP_D3D11_OCC`, `RECOMP_D3D11_NO_RTT`) in this menu.
   - **Persistence and presets**
     - [x] Save the menu to `breakdown_video.cfg` beside the exe and apply it on the next launch through the `RECOMP_*` host path. A set variable wins over the file. An unset variable uses the file. Neither stays at the stock defaults. `run_native.cmd` still sets its variables, so that launch keeps winning until those lines are removed.
     - [x] Low and High on the Video menu, inside the same clamps. They are not extra config keys. Low writes `RECOMP_RENDER_SCALE=1`, `RECOMP_WINDOW_SCALE=1`, `RECOMP_PRESENT_FILTER=linear`. High writes `RECOMP_RENDER_SCALE=4`, `RECOMP_WINDOW_SCALE=2`, `RECOMP_PRESENT_FILTER=integer`. Aspect and window mode stay as they are. Anything else reads as Custom, and the individual rows stay editable. `run_native.cmd` is still 2× / 16:9 and still wins when those variables are set.
5. **Accessibility** — first-class P1, with the video menu. Not deferred to P2, P3, or the parallel tracks.
   - [x] HUD / UI readability inside the 4:3 frame. F10 → HUD readability sets `RECOMP_RENDER_SCALE` and `RECOMP_WINDOW_SCALE` to the same step, 1x–4x. The HUD is in the same colour target as the scene, so this enlarges that frame. It does not move the HUD into the letterbox. A HUD that leaves the 4:3 image is hor+ (item 2), not this row.
   - [ ] Contrast and colorblind-friendly cues. Not shipped: the present path only selects nearest, linear, or integer. This tree has no colour transform and no per-element overlay, and a flat tint would not be a colorblind cue. No checkbox until one of those paths exists. Replacement art stays P2.
   - [ ] Subtitles / captions. Not shipped. Spoken lines are not text in this repo (the disc dump and generated recomp are not in git, and `src/` has no dialogue script). Audio only, until a line exists as text. This does not add a subtitle track.
   - [ ] Control remapping. Not this menu. Full remapping stays on the parallel controls track below (in-game remap UI is out of scope for the current mouse/keyboard track; see [docs/mk-controls-design.md](docs/mk-controls-design.md)). Shipped mouse/keyboard defaults stay in `CONTROLS.txt`. The remapper has to cover this need when it lands.
   - [ ] Comfort options: motion and flicker toggles once a real control exists (after the P0 flicker fix, or a host option that actually changes comfort). This pin has no motion-blur, camera-shake, or flicker switch to expose, and those investigation switches stay off the menu.

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