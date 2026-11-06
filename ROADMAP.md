# Breakdown native PC — roadmap

Unofficial fan / preservation project. Own the game. No dumps in git.

Severity tags use [PRIORITY_MATRIX.md](PRIORITY_MATRIX.md): Crash = critical, Unplayable = high, Poor performance = high, Medium = functionality isn't working properly but still playable, Low = cosmetic / overall bugs that don't impact play.

## Priority order

### P0 — Stability
1. **Fix flickering textures** (likely lighting + texture / occlusion interaction). Playable image first.
   Severity: Medium — playable with texture flicker, and with shadow flicker/vanish on turn (no perf hit).
   Notes: [docs/flicker-investigation.md](docs/flicker-investigation.md). The exe defaults occlusion queries to `sync` (lights and overlays). Mip and render-target defaults were left alone.

### P1 — Display
Player-facing **Options → Video** (and an accessibility section in that same priority). Same kind of in-game control a modern PC FPS ships: the menu is required, and env vars / `run_native.cmd` stay the host path underneath it. Accessibility is a P1 must-have, not a later pass.

2. **Modern aspect ratios** — 16:9, 16:10, ultrawide where feasible, with HUD-safe framing.
   Severity: Low — letterboxed 16:9, 16:10, and 21:9 already play. Open hor+ does not block play.
   - [x] Letterbox the 4:3 frame in a 16:9, 16:10, or 21:9 window (`RECOMP_DISPLAY_ASPECT`). HUD stays in the 4:3 image. [docs/widescreen-hires.md](docs/widescreen-hires.md).
   - [ ] Hor+ (a wider camera). This toolkit pin logs any `display.aspect` other than 4:3 and keeps 4:3. Still its own item: the video menu may offer letterboxed aspects now, and offers hor+ only once this pin implements it.
     Severity: Low — wider camera. Letterbox play is intact.
3. **Higher internal / output resolutions** beyond the Xbox default.
   Severity: Medium — exclusive fullscreen acts as borderless. Render scale and window scale already play.
   - [x] `RECOMP_RENDER_SCALE` 1..4 and `RECOMP_WINDOW_SCALE` 1..6. `run_native.cmd` uses 2 and 16:9.
   - [ ] Modes other than borderless fullscreen (`RECOMP_PRESENT_FULLSCREEN=1` is the borderless switch). Exclusive fullscreen is not on this present path yet.
     Severity: Medium — exclusive fullscreen acts as borderless (`SetFullscreenState` fails). Borderless is still playable.
4. **In-game Video / Graphics menu** — full in-game control, not env-only. Required so weaker PCs can choose performance against clarity. The menu persists choices and applies them through the existing `RECOMP_*` host knobs (launch scripts keep working; a normal launch reads the saved config when those vars are unset). Items below are the parity goals. They are not all implemented. This slice ships the Video rows and the config file ([docs/video-options.md](docs/video-options.md)). Accessibility, presets, VSync, exclusive fullscreen, and hor+ are still open.
   Severity: Medium — F10-to-menu / pause opens Video is in progress (PR #8). Discoverability; F10 still works. Feature work, not a crash.
   - **Display**
     - [x] Resolution and window mode: windowed and borderless, from the Video menu (F10) and from `breakdown_video.cfg` beside the exe. Sizes are `RECOMP_WINDOW_SCALE` 1..6 × `RECOMP_DISPLAY_ASPECT`. The menu calls the second mode borderless. Exclusive fullscreen is still item 3 and is not offered.
     - [x] Aspect: letterbox `4:3`, `16:9`, `16:10`, or `21:9`. Hor+ is not offered (item 2).
     - [ ] VSync and a frame cap, once the present path can support them. `RECOMP_PRESENT_PACING` only changes how lowered spin-waits sleep. It is not this option.
       Severity: Low — no frame cap yet. Play stays at a usable framerate.
     - [x] Present filter (`RECOMP_PRESENT_FILTER`): nearest, linear, or integer, from the same menu. A wide window at render scale 1 with an explicit nearest filter still copies to the top left; the menu says so. Presets that would force linear or integer at that combination are the next slice.
   - **Graphics quality**
     - [x] Render scale / internal resolution (`RECOMP_RENDER_SCALE` 1..4), from the same menu. This is the main cost lever (targets and the present texture grow with the scale). A change is requested for the next frame (`nv2a_host_opts_request`); it does not need a new process.
     - [x] Texture and filter quality where the stack allows a safe player control. The present filter above is that control. In-scene mip and point-sample switches (`RECOMP_D3D11_NO_MIPS`, `d3d11_point`) stay off the menu until they are shown not to bring flicker back. See [docs/flicker-investigation.md](docs/flicker-investigation.md).
     - [ ] Effects toggles that change performance on weaker PCs, only for an effect the host can cheapen without a new art or post-process pipeline. Do not put flicker investigation switches (`RECOMP_D3D11_OCC`, `RECOMP_D3D11_NO_RTT`) in this menu.
       Severity: Low — optional cheaper effects. Current play stays at a usable framerate.
   - **Persistence and presets**
     - [x] Save the menu to `breakdown_video.cfg` beside the exe and apply it on the next launch through the `RECOMP_*` host path. A set variable wins over the file. An unset variable uses the file. Neither stays at the stock defaults. `run_native.cmd` still sets its variables, so that launch keeps winning until those lines are removed.
     - [ ] Ship a low-end preset and a high-end preset inside the same clamps (render scale 1..4, window scale 1..6, the filters above). Low-end favours a smaller internal resolution and a cheap window; high-end favours a higher render scale and a sharper present. `run_native.cmd` can keep its current 2× / 16:9 launch.
       Severity: Low — presets are convenience. Manual scales already play.
5. **Accessibility** — first-class P1, with the video menu. Not deferred to P2, P3, or the parallel tracks.
   Severity: Medium — contrast, captions, and comfort toggles are missing. The game is still playable. The controller gate is Unplayable on the mouse/keyboard track.
   - [ ] HUD / UI scale or readability inside the 4:3 frame. The HUD is drawn into the same colour target as the scene, so it scales with render scale and the letterbox does not move it. The menu exposes a readability control on that frame (internal scale and window size). A HUD that leaves the 4:3 image is hor+ (item 2), not this checkbox.
     Severity: Low — extra readability control. Render scale already changes the frame.
   - [ ] Contrast and colorblind-friendly cues where they can be done without rewriting the art pipeline (present-time or a simple overlay). Replacement art stays P2.
     Severity: Medium — colorblind cues are missing. Play still proceeds.
   - [ ] Subtitles / captions when the game has dialogue or spoken lines that can be surfaced. This does not add a new script. If a line exists only as audio with nothing to show, record that limit here instead of inventing a track.
     Severity: Medium — captions are missing where lines exist. Play still proceeds.
   - [ ] Control remapping. Full remapping stays on the parallel controls track below (in-game remap UI is out of scope for the current mouse/keyboard track; see [docs/mk-controls-design.md](docs/mk-controls-design.md)). Accessibility in this item does not wait on that track. Shipped mouse/keyboard defaults stay reachable until the remapper exists, and the remapper has to cover this need when it lands.
     Severity: Low — rebinding is a missing extra. The controller gate is Unplayable on the mouse/keyboard track.
   - [ ] Comfort options: motion and flicker toggles once a real control exists (after the P0 flicker fix, or a host option that actually changes comfort). This pin has no motion-blur or camera-shake switch to expose.
     Severity: Medium — no flicker or motion comfort toggle yet. Flicker itself stays Medium while play continues.

### P2 — Presentation
6. **Anniversary-style graphics toggle** — switch between original look and touched-up assets (like Halo CE Anniversary original/remastered). Default: original. Updated pack optional / generated locally from your dump where possible.
   Severity: Low — optional art toggle. Default stays the original look. Cosmetic.

### P3 — Speedrun / competitive
7. **Built-in timer + splits/stats** for categories such as Burger, 100%, Any%, etc.
   Severity: Low — speedrun timer and splits. Does not change play.
8. **In-game leaderboard** for players who opt in to post times.
   Severity: Low — opt-in leaderboard. Does not change play.
9. Hook into a future **multiplayer / competitive** mod where it fits.
   Severity: Low — future competitive hook. Single-player play is unchanged.

### Parallel / next after P0–P1
- Mouse/keyboard (Modern punches LT/RT; auto-apply patches) — in progress
  Severity: Unplayable (high) — mouse/keyboard gated on controller presence. M&K-only (no pad) cannot control the game.
- Full remapping + Legacy vs Modern control schemes (keep original melee combos)
  Severity: Low — schemes are not switchable in game. Shipped defaults are the play path when input works.
- Character skin swaps in Extras
  Severity: Low — cosmetic.
- Platform notes: Linux/SteamOS, then Android/iOS/web feasibility
  Severity: Low — feasibility notes. Windows play is unchanged.

## Working rule
If a decision needs the owner and they don’t answer in ~5 minutes, pick the best recommendation and keep shipping.