# Breakdown native PC — roadmap

Unofficial fan / preservation project. Own the game. No dumps in git.

## Priority order

### P0 — Stability
1. **Fix flickering textures** (likely lighting + texture / occlusion interaction). Playable image first.
   Notes: [docs/flicker-investigation.md](docs/flicker-investigation.md). The exe defaults occlusion queries to `sync` (lights and overlays). Mip and render-target defaults were left alone.

### P1 — Display
2. **Modern aspect ratios** — 16:9, 16:10, ultrawide where feasible, with HUD-safe framing.
3. **Higher internal / output resolutions** beyond the Xbox default.

### P2 — Presentation
4. **Anniversary-style graphics toggle** — switch between original look and touched-up assets (like Halo CE Anniversary original/remastered). Default: original. Updated pack optional / generated locally from your dump where possible.

### P3 — Speedrun / competitive
5. **Built-in timer + splits/stats** for categories such as Burger, 100%, Any%, etc.
6. **In-game leaderboard** for players who opt in to post times.
7. Hook into a future **multiplayer / competitive** mod where it fits.

### Parallel / next after P0–P1
- Mouse/keyboard (Modern punches LT/RT; auto-apply patches) — in progress
- Full remapping + Legacy vs Modern control schemes (keep original melee combos)
- Character skin swaps in Extras
- Platform notes: Linux/SteamOS, then Android/iOS/web feasibility

## Working rule
If a decision needs the owner and they don’t answer in ~5 minutes, pick the best recommendation and keep shipping.