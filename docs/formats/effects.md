# Visual effects, render-to-texture passes and what differs on PC

Worked out statically (2026-11-14). Nothing was run. Companion to `lighting.md`.

**Short version.** Effects come from four places: effect events in the level files (fires, smoke, dust, sparkles),
game code that spawns effects for actions (explosions, kicks, the time tunnel), the light events that draw
things (light cones, glows, water reflections), and full-screen filters at the end of the frame (glow blur, haze,
fade, noise). The off-screen passes are shadows (4 targets), glow (2 targets), water reflection and the filters.

---

## 1. Effect events in the level files

`EV_TYPE_EFFECT`: **491** events. Reader: **`EffectEv_Create` 0xB32B0**.

| Field | Default | At (event+) | Meaning |
|---|---|---|---|
| `LEEFFECT` | 0 | 0x1EA (u16) | which effect (table 1.1) |
| `EFF_POWER` | 1 | 0x140 | strength / emit rate (0..120 seen) |
| `EFF_SIZE_X/Y/Z` | 1 | 0x144..0x14C | size of the area or puff |
| `EFF_LIFE_SPAN` | 1 | 0x150 | particle life (20..800) |
| `EFF_DIR_X/Y/Z` | 1 | 0x154..0x15C | direction (not in any file: default) |
| `EFF_CX/CY/CZ`, `EFF_FLOOR` | 1 | 0x160..0x16C | smoke column: line count, spacing, floor height |
| `EFF_WX/WY/WZ` | 0 | 0x180..0x188 | wind / spread (WX falls back to `ROTATION_Y`) |
| `EFF_ALPHA`, `EFF_BETA` | 1 | 0x170, 0x174 | opacity / second factor |
| `EFF_WIDTH`, `EFF_HEIGHT` | 1 | 0x178, 0x17C | not in any file |
| `EFF_MODE` | 1 | 0x1E8 | not in any file |
| `EFF_LG_LENGTH/ANGLE/PITCH`, `EFF_LG_SW_PROJECTION_MAP` | 0 | 0x1F0..0x1FC | light-cone values (read but unused for most ids) |
| `EFF_LIGHTOFF` | 0 | | 1 = no flicker light for fires (68 events) |
| `HAITI_FLG`, `BASE_FLG`, `EFF_SHAPE` | | | placement flags of the level tool (EFF_SHAPE always 0) |
| `MODE` | | 0x34 | 2 on at start, 1 waits for a trigger |

**Fire light.** For ids 0 and 2 (fires) without `EFF_LIGHTOFF`, the reader adds a POINT light (orange
0.98/0.54/0.15, near 10, far 150, 10 units above the fire) and flickers its colour every frame with the game's
random number (`Rand` 0x1946B0) in `EffectEv_Update` 0xB30D0.

**Update.** `EffectEv_Update` 0xB30D0 runs when the effect is on screen (0x251C0; id 0x17 always): ids 0, 0xE,
0xF, 0x10 → `EffectEv_EmitFire` 0xB27D0 (0xB4B20 / 0xB4C20, cycling through 30 / 14 frames); the rest →
`EffectEv_Emit` 0xB28D0 (a switch over ids 0..0x29, each spawning particles every N frames from the global frame
counter 0x38279C). Id 0x27 also draws via 0xB1080.

### 1.1 Effect ids (from the level tool's names)

| Id | Name in files | Count | Areas | Handled by |
|---|---|---|---|---|
| 0 | `fire` | 105 | gesui, hill, park.. | fire + flicker light |
| 1 | `cloud` | 33 | hill, silo.. | 0xC0C90 every 10 frames |
| 2 | `fire_yoko` (sideways fire) | 8 | bettou, gesui, hon_hon | + flicker light |
| 5 | `hokori` (dust) | 3 | gesui, hon_hon | generic |
| 8 | `shinning` (glint) | 40 | hill, park | `KM_EVENT` 0x150E70 |
| 13 | `tatesen` (vertical lines) | 5 | park3 | generic |
| 14 | `fire_a` | 7 | bettou, silo | fire |
| 15 | `fire_b` | 96 | bettou, hon_hon, park | fire (0xB4C20) |
| 16 | `fire_c` | 21 | hon_hon, park | fire (0xB4C20) |
| 17 | `pipe` | 5 | kyodo1 | `KM_EVENT` |
| 18 | `shinning` | 51 | many | `KM_EVENT` |
| 21 | `ductsmoke` | 1 | gesui | generic |
| 23 | `spansmoke` | 33 | hill, hon_under, park | generic (always updates) |
| 25 | `dust` | 17 | shaft, tunnel2 | `KM_EVENT` |
| 29 | `spanyuge` (steam) | 5 | park3 | generic |
| 30 | `smoke` | 14 | hon_hon | **ko_eff_smoke** (1.2) |
| 31 | `navi_warp` | 4 | hon_under | `tkEffectInitLE` 0x172340 |
| 34 | `linekemu` (smoke line) | 4 | bettou, hon_under, labo1 | generic |
| 35 | `flash_timetunnel` | 1 | silo | `tkEffectInitLE` |
| 36 | `navi_tears` | 1 | silo | `tkEffectInitLE` |
| 37 | `swing_back` | 3 | ending, future2 | `tkEffectInitLE` |
| 38 | `event_hokori` | 2 | bettou | `KM_EVENT` |
| 40 | `hibana_th` (sparks) | 7 | future, silo | generic, own sound/light (0x239A0) |
| 41 | `sazin` (sand dust) | 10 | hon_hon | `KM_EVENT` |
| 42 | `tate_line` | 2 | silo | `KM_EVENT` |
| 43 | `mizusimuki` (water spray) | 12 | gesui | `KM_EVENT` |
| 44 | `shinning` | 1 | plant | `KM_EVENT` |

Dispatch in the reader: ids 0x11, 0x12, 0x19, 0x1A, 0x1B, 0x20, 0x26, 0x2A, 0x2B, 0x2C → `KmEffect_Init` 0x151280
(`KM_EVENT`, update 0x150E70); 0x1E → 0x10EBC0 → ko_eff_smoke; 0x1F, 0x23, 0x24, 0x25 → `tkEffectInitLE` 0x172340.

### 1.2 Per code file (leaked source names)

| File | Entry | What it draws |
|---|---|---|
| `kondoh/ko_eff_smoke.c` | `KoEffSmoke_Create` 0x10DF20 | smoke columns: `EFF_CX` lines × 0x5C bytes (`KO_EFFSMOKE_MEM`, max checked: "Line Max Over"), puffs on a sphere list (`KO_EFFSMOKE_SPHERE`) rising from `EFF_FLOOR`; count = LIFE_SPAN × POWER. Steps: update 0x10DA00, draw 0x10DC90, end 0x10DEF0 |
| `kondoh/ko_eff_lockon.c` | `KoEffLockon_Create` 0x10EB30 | the lock-on marker on a target (`LOCKON` heap; 0x10E410 update, 0x10E680 draw) |
| `kondoh/ko_ncc_eff.c` | 0x10A760 | effect for the NCC sequence (loads `areahit_load_DE`) |
| `kondoh/ko_ncc_laser.c` | 0x10AA30 | laser points (`KO_NCC_POINT_HEAP`) |
| `kondoh/ko_sunrise.c` | `KoSunrise_Update` 0x10BDD0 | sky/scene colour over time: up to 6 keys (time, R, G, B) at `[0x3EA038]`, blended between keys; mode 1 = playing, 2 = done |
| `kondoh/ko_event_effect.c` | 0x10F600 / 0x10F650 / 0x10F720 | table of scripted event effects ("EFFECT"; errors "seq over", "func pointer error", "event id over"); heap `KOD_BSMOKE_HEAP` |
| `kondoh/ko_debug.c` (0x1108D0) | | debug smoke placer (`KOD_BSMOKE%s_%s`, `LIFESPAN`, `F_SIZE`) |

**"tk" effects** (0x16D000–0x173000, heap names `TK_EFC_*`): hero deadly blow (+ripple), discharge cartridge,
flash time tunnel, swing back (+light), navi warp (+light, ripple), spray kick (+blur, drop), wind chopper,
chopper window smoke, explosion (smoke, smoke2, flame, spark), wave (mist, water), navi tears (+mark), and a test
effect. Their names are a table of strings at 0x274550..0x274578. Chopper damage smoke and the truck effect tool
live nearby (`TK_EFC_TOOL` 0x1620C0).

Effects with a light also call `Light_Init` / `Light_Register` (callers of 0x164A0: 0xB7530, 0xC0470.. 0xC0960,
0x146680, 0x147560, 0x16F3C0, ...), so muzzle flashes and explosions light nearby models the same way lamps do.

---

## 2. Light events that draw

From `lighting.md`: SPOTEFFECT (cone mesh, 0x30C50), GLOW / GLOW_LITE (sprites, 0x30D70 / 0x30EC0), OVERLAY
(screen tint), TFOG (haze settings), WATERREF (mirror matrix, 0x31150, 62 of them, mostly gesui).

## 3. Render-to-texture passes

Targets are made by `Gfx_CreateTarget` 0x18D2D0 (id, width, height, format code, 1).

| Pass | Target(s) | Made by | Drawn by | Notes |
|---|---|---|---|---|
| Shadow buffers | 0x4001, 0x4002 512×512; 0x4003, 0x4004 256×256 | `ShadowMgr_Create` 0x304D0 | `Shadow Draw` task 0x30680 (steps 0x30310/0x30320), shadow lib 0x184xxx | light-space boxes ±14 / ±55 / ±140, depth 0.1–500; uses Xbox depth compare (`ShadowFunc` GREATER) |
| Glow | 0x4005 256×256 | `GlowMgr_Create` 0x141D0 | `GlowMgr_Draw` 0x12A00 | glow models/sprites, blurred (0x182950, 0x182B00), added to screen |
| Glow small | 0x4027 64×64 | `GlowMgr_Create` | glow sprite path 0x181560 | downsample chain to ≤ 8 px |
| Haze / light shafts | (library 0x1A6A60–0x1A7EF0) | | `GlowMgr_Draw` | TFOG values, wind |
| Water reflection | mirror matrix | `LightEv_Create` kind 23 | 0x31150 | |
| Screen filters | screen copy (0xFFFFFFF0/F1/F2 = screen surfaces) | | 0x180680, 0x180140 (fade), 0x17FFF0 | `Gfx_SetFilterShader` 0x17FA60 sets the pixel shader |
| Other targets | 0x279C0, 0x188930 | | | not traced |

`D3DDevice_SetRenderTarget` has 17 callers (0x180820, 0x180B10, 0x180D40, 0x181000, 0x181560, 0x1819A0,
0x181CE0, 0x1821D0, 0x182B00, 0x182C30, 0x183870, 0x183A20, 0x18A200, 0x190AF0, 0x190BE0, 0x190C40, 0x195D80):
the filter library (0x180000–0x183000) owns most of them. This matches the ~26 target switches per frame measured
on PC (kit `reports/research/S8-perf-findings.md`): shadows (up to 4), glow build + blur steps, sprite-glow
downsample steps, filters.

Character shadows on PC also show **shadow volumes** drawn into destination alpha (renderer comment at
`nv2a_pb_d3d11.c` "Character shadow volumes"), separate from the shadow buffers above.

---

## 4. Only checkable by running the game (for later)

1. Which blink curve each `LIGHT_BL_TYPE` 1–4 gives (watch a blinking lamp, e.g. bettou).
2. How many lights a typical character/room piece really gets (break on 0x16740, read the return value).
3. Whether "Glow Filter" / "Glow Filter Min" switches change the glow pass, and "EffectObj Debug Disp" output.
4. What the haze library 0x1A7C40.. draws on screen (bettou_D `lelight_tfog_*`, extra_back).
5. Which pass makes each of the ~26 target switches per frame (log `SetRenderTarget` caller + target id).
6. Whether the shadow buffers come out right on PC (see 5.1), compared with xemu.
7. Fog fade speed between rooms (10 % per frame means it is tied to frame rate: check at 60 fps).
8. Sunrise colour keys in the ending.

## 5. Likely PC-vs-Xbox differences (from reading the renderer, not yet seen in game)

1. **Shadow-buffer compare is not emulated.** The game turns on `ShadowFunc` (GREATER) to sample depth targets
   as shadow maps. In `external/xboxrecomp/src/d3d` there is no handling of the NV2A shadow compare, and
   `d3d8_resources.c` says depth textures are not sampled in D3D11. Shadows from the shadow buffers may be missing,
   solid, or wrong. (Character shadow volumes into destination alpha are handled; see the `rasterizer_state`
   comment.)
2. **Depth precision**: PC uses a 32-bit float depth buffer, not the Xbox's 24-bit fixed (comment near
   `nv2a_pb_d3d11.c:1226`). Fine for drawing, but shadow-depth comparisons and z-fighting may differ slightly.
3. **Fog is computed in the translated vertex shader** (`d3d8_vsh_hlsl.c` lines ~470: linear / exp / exp2 from
   `fog_param`); the NV2A's exact fog table and clamping may differ at the far end (check far fog in big areas,
   e.g. bettou `FOG_FAR_BLK` 135..37,568).
4. **Render targets are recreated / resized on PC** (`rt_grow`, `rt_park`, render scale): glow and filter passes
   that read the screen at 256×256 / 64×64 will look softer or sharper at higher resolutions, and sub-rectangle
   copies (`rt_sub_copy`) may shift by a pixel.
5. **Frame-rate tied effects**: fog fade (10 % per frame), effect emit timers (every N frames from 0x38279C) and
   blink speed use frame steps, so at 60 fps they run twice as fast unless the game's frame time (0x29BD38) is
   used. Blink and fade use 0x29BD38 (fine); fog lerp and emit counters do not.
6. **Specular and lighting** are in the game's own vertex shaders, translated by `d3d8_vsh`; they should match
   the Xbox closely. Per-pixel lighting or sharper shadows would be new work (an "Enhanced" option), not a fix.
