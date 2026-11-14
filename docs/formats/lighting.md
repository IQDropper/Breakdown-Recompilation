# Lighting, fog and the glow filter

How Breakdown lights its rooms, worked out statically (2026-11-14) from the level files and the decompiled code.
Nothing was run. Addresses are in `default.xbe`; names are in `config/function_names.csv`.

**Short version.** Every room's look comes from three things:

1. **Baked light** in the room models' vertex colours ("PRE" lights, already painted in by the developers' tool).
2. **Live lights** from the level files: up to 8 per object, picked each draw, sent to the game's own vertex
   shaders as constants (no fixed-function D3D lights are used: the game never calls `SetLight`).
3. **Fog** per room (near and far), faded smoothly as you walk between rooms, plus a screen-wide
   **glow/haze pass** at the end of each frame.

Tool: `python tools/light_dump.py game_files/romdata/area/<area>` lists every light with the game's defaults.

---

## 1. Light data in the level files

Lights are `EV_TYPE_LIGHT` events in `romdata/area/<area>/<area>[_X].evd` (format: `tools/evd_dump.py`).
There are **3,736** of them in 23 areas (hon_under = 7F/Main Research Center has 109).

The reader is **`LightEv_Create` 0x316A0**. It reads each field by name; when a field is missing it uses the
default in the table below (so the tool fills these in too).

| Field | Type | Default | Stored at (event + ) | Meaning |
|---|---|---|---|---|
| `NAME` | string | | | `lelight_<kind>_NNN` (tool-made name) |
| `LIGHT_FLAG` | int | 0 | 0x100 | **0 = POST** (live), **1 = PRE** (already baked into vertex colours). Names from table 0x2412B0. 3,490 PRE, 145 POST. |
| `LIGHT_KIND` | int | 0 | 0x104 | kind, see 1.1 |
| `LIGHT_COL_R/G/B` | float 0..1 | 1.0 | 0x108..0x110 | colour |
| `POSITION_X/Y/Z` | float | 0 | 0x90..0x98 (common event block) | world position (also picks the room) |
| `ROTATION_X/Y/Z` | float deg | | common block | the editor's rotation; the game uses `LIGHT_DIR` |
| `LIGHT_DIR_X/Y/Z` | float, unit | 0 | 0x128..0x130 | direction (directional, shadow, cone, glow) |
| `LIGHT_AMBIENT` | float | 1.0 | 0x114 | ambient share (also "d0" of the haze filter) |
| `LIGHT_DIFFUSE` | float | 1.0 | 0x118 | diffuse strength (multiplies colour) |
| `LIGHT_SPECULAR` | float | 1.0 | 0x11C | specular strength (multiplies colour) |
| `LIGHT_NEAR` | float | 10 | 0x120 | full-strength radius (point) / fog start |
| `LIGHT_FAR` | float | 60 | 0x124 | zero radius (point) / fog end |
| `FOG_NEAR`, `FOG_FAR` | float | 0, 1 | 0x140, 0x144 | haze filter only (kinds 20/21) |
| `LIGHT_BL_TYPE` | int | 0 | 0x19C | blink pattern; 0 = steady (2,219 lights), 1..4 used by 16 |
| `LIGHT_BL_SPEED` | float | 1.0 | 0x1A0 | blink speed |
| `EFF_LG_*`, `EFF_TEXTUREID` | | per kind | 0x118..0x138, 0x1A8 | extra values for cones, glows, haze, water (1.2) |
| `MODE` | int | | 0x34 (event state) | 2 = on from the start (4,171), 1 = waits for a trigger |
| `NAME_ID` | string | | | story-scene tag on a few lights |

Value ranges seen in the files: colours 0..1; position X -11,211..7,877, Y -3,020..5,240, Z -27,889..6,829;
`LIGHT_DIFFUSE` -0.47..20; `LIGHT_SPECULAR` -2..2 (negative on haze lights); `LIGHT_NEAR` -1,000..2,500;
`LIGHT_FAR` -173..50,000. Field `MODE` appears twice in some events (the game takes the first). One event has a
stray field named `e`. Five `lelight_block_ambient_far_*` / one `..._directional_far_*` have no kind, so the game
treats them as kind 0 (a white point light 10..60 at 0,0,0, which lights nothing in practice).

### 1.1 Kinds

Names 0..14 are the game's own (string table 0x241270, used by the LIGHT_MANAGER debug page 0x16E80).
15..23 are named from the level tool's event names. "Room" kinds attach to the room (cell) they stand in
(`Light_AttachCell` 0x165F0 calls `Cell_FromPosition` 0x24DD0); "section" (`_BLK`) kinds apply to the whole loaded
section. Count = number in all level files.

| Kind | Name | Count | Where | What the game does with it |
|---|---|---|---|---|
| 0 | POINT | 1,313 | section list, chosen by distance | point light, fades from NEAR to FAR |
| 1 | DIRECT | 498 | room | room key light (directional) |
| 2 | AMBIENT | 369 | room | room fill colour (replaces the section fill) |
| 3 | DIRECT_BLK | 172 | section | section key light (directional); the brightest one becomes the main light |
| 4 | AMBIENT_BLK | 115 | section | section fill colour |
| 5 | AMBIENT_CTRL | 3 | room | colour **added** to the room fill |
| 6 | FOG | 187 | room | room near fog (colour, NEAR/FAR) |
| 7 | FOG_FAR | 8 | room | room far fog |
| 8 | FOG_BLK | 114 | section | section near fog |
| 9 | FOG_FAR_BLK | 29 | section | section far fog |
| 10 | SHADOW | 415 | room | the room's shadow direction (stored in cell+0x1D4) |
| 11 | SHADOW_BLK | 140 | section | section shadow direction (shadow manager +0x44) |
| 12 | TLIGHT | 5 | room | timed tint/flash light (own draw step 0x313E0) |
| 13 | TLIGHT_BLK | 19 | section | same, section-wide (0x309E0) |
| 14 | SPOTEFFECT | 29 | room | a visible light cone mesh (0x30C50); `EFF_LG_LENGTH` 30, `PITCH` 1, `ALPHA` 0.8, `ANGLE` 30, `SW_PROJECTION_MAP` |
| 15 | GLOW | 166 | room | glow sprite / lens glow (0x30D70); `EFF_TEXTUREID`, `ALPHA` 1, `MASK` 0.1, `OFFSET` 5 |
| 16 | SHADOW_EX | 5 | section | extra shadow box: SCALE = box size, NEAR/FAR = depth (0x30120) |
| 17 | GLOW_LITE | 43 | section | cheaper glow sprite (0x30EC0), same fields as 15 |
| 18 | OVERLAY | 4 | room | screen colour overlay (0x31400) |
| 19 | OVERLAY_BLK | 2 | section | same, section-wide (0x30A80) |
| 20 | TFOG | 33 | room | haze/depth filter settings (0x31440), feeds the glow manager |
| 21 | TFOG_BLK | 5 | section | same, section-wide (0x30AC0) |
| 22 | – | 0 | | has a draw step (0x30B70) but no light uses it |
| 23 | WATERREF | 62 | room | water reflection plane, builds a mirror matrix (0x31150); `ALPHA` 0.6, `ANGLE` 25 |

Draw distance for the effect kinds (event+0x1A8): 1,000 for cones and water, 2,000 for glows, 1,000,000 when the
event is created with load type 5.

---

## 2. Life of a light

| Step | Function | What happens |
|---|---|---|
| read | `LightEv_Create` 0x316A0 | reads the fields above, builds the 0x54-byte **light record** at event+0x148 with `Light_Init` 0x164A0 |
| wait 2 frames | `LightEv_Start` 0x31460 | then `Light_AttachCell`, and picks the per-kind update/draw step (switch on kind) |
| switch on/off | `LightEv_Update` 0x312B0 (kinds 0–5) | event state 2 → `Light_Register` 0x16300, 3 → `Light_Unregister` 0x16360, 4/5 = fade out over 2 s; blink: `LIGHT_BL_SPEED` × time drives the diffuse (event+0x180) |
| fog kinds | `LightEv_FogUpdate` 0x30910 | 6 → room near fog, 7 → room far fog, 8/9 → section fog slot 0 |
| shadow kinds | `LightEv_ShadowUpdate` 0x30770 | 10 → cell+0x1D4, 11 → `Shadow_SetSectionLight` 0x301D0, 16 → `Shadow_SetExtraBox` 0x30120 |
| delete | `LightEv_Destroy` 0x31190 | removes the record from the manager and the room |

**Light record** (0x54 bytes, `Light_Init` 0x164A0):

| + | Field |
|---|---|
| 0x00 u16 | LIGHT_FLAG (0 POST / 1 PRE) |
| 0x02 u16 | on (1) / off (0) (table 0x2412B8 "OFF"/"ON") |
| 0x04 | 0 |
| 0x08 int | kind |
| 0x0C ptr | room (cell) or 0 |
| 0x10/14/18 | colour R G B |
| 0x1C/20/24 | position |
| 0x28/2C/30 | direction |
| 0x34/38/3C | ambient / diffuse / specular |
| 0x40/44 | near / far (if far ≤ near: far = near + small step, range 0.1) |
| 0x48/4C | extra 1 / 2 (cone angle, glow texture, haze power) |
| 0x50 | far − near |

**Where lights are kept.** `LIGHT_MANAGER` (made by `LightMgr_Create` 0x17000, pointer at 0x29CC40) has
**256 slots** at +0x64 for section-wide lights. Each room (cell) has **8 slots** at cell+0x1B4..0x1D0 for its own
lights, plus the shadow light at cell+0x1D4. Manager +0x40 = default ambient, +0x50 = ambient for pre-lit
objects (`LightMgr_SetAmbient` 0x16440 / `LightMgr_SetPrelitAmbient` 0x16470, set per area by 0x1BE70).
`LightMgr_GetMainDir` 0x15F30 returns the main directional light's direction (for shadows and specular).

---

## 3. Choosing lights per object and sending them to the GPU

**`Light_SetupForObject` 0x16740** `(cell, position, radius, max lights ≤ 8, flags, pre-light scale)` is called
before drawing a model (23 callers: characters, objects, room pieces). It:

1. Turns all 8 GPU light slots off (`GfxLight_Enable(i, 0)` 0x18FA30).
2. Starts the ambient from manager +0x40, or +0x50 when the object is pre-lit (flags bit 0).
3. Walks the 256 section lights:
   - **POINT**: candidate if `distance < radius + FAR`; score = brightest colour channel × falloff
     (`Light_Score` 0x166E0: full until NEAR, linear to 0 at FAR).
   - **DIRECT_BLK**: the main directional light (score = brightest channel).
   - **AMBIENT_BLK**: replaces the ambient.
   - Lights flagged **PRE are skipped for pre-lit objects** (they are already in the vertex colours).
4. Walks the room's 8 lights: DIRECT → candidate, AMBIENT → replaces ambient, AMBIENT_CTRL → added to ambient.
5. Sets the ambient (`GfxLight_SetAmbient` 0x18FBE0) and keeps the **N best** by score (N = max lights, ≤ 8).
6. For each kept light writes a GPU light slot: type (point = 1, directional = 3), position, direction,
   NEAR/FAR, diffuse = colour × DIFFUSE, specular = colour × SPECULAR (both × pre-light scale for PRE lights).
7. Flags bit 2: slot 7 becomes a **rim light** opposite the camera (direction from 0x29BC50, colour
   `(a − a²) × k` of the ambient).
8. Returns how many lights it set (directional count − 1 + point count).

Debug switch **"Back Ground PreLight"** (debug-switch bit 2, read by `DebugSw_BgPrelight` 0x25970) is checked only
here: when on, it clears the pre-lit flag, so room models get **all** lights live, including PRE ones (a way for
the developers to preview the baked lights). So "PreLight" = vertex colours baked offline, not a runtime pass.

**GPU light slots.** The render library keeps 8 slots of 0x80 bytes at `[0x435240]` (count `[0x435260]`):

| + | Field | Setter |
|---|---|---|
| 0x00 | type 1 point / 3 directional | 0x18FA70 |
| 0x04 | diffuse RGBA | 0x18FB60 |
| 0x14 | specular RGBA | 0x18FBA0 |
| 0x34 | position | 0x18FAA0 |
| 0x40 | direction | 0x18FAD0 |
| 0x4C / 0x68 | far / near | 0x18FB00 |
| 0x54..0x5C, 0x6C | attenuation (0x18F9A0) / range | 0x18FB30 |
| 0x70 | specular power (else the material's) | |
| 0x74 | 0 = on, −1 = off | 0x18FA30 |

Ambient is at 0x435250.

**Upload: `GfxLight_UploadVS` 0x190020**, per draw, for the bound vertex shader (its descriptor says how many
directional and point slots it has): directional lights go through `GfxLight_WriteDirVS` 0x18FCA0 into **4 vertex
shader constants each, counting down from c62** (direction in object space, a Blinn half-vector with the eye
at 0x4399E0, diffuse, specular, power); point lights go through 0x18FF50 → `GfxLight_WritePointVS` 0x18FE00
(object-space position, diffuse, specular, and either range or attenuation, chosen by flag 0x27C579). Unused slots
are filled with zero constants. So **lighting is per vertex**, in the game's own vertex shaders, with
Blinn-style specular. Nothing per pixel.

---

## 4. Fog

**FOG_MANAGER** (`FogMgr_Create` 0x15A40, pointer 0x29CC30) keeps one block of 0xEC bytes per view (up to 4
views, count at 0x29BD48), each with a **near fog** and a **far fog** setting of 8 floats
`(mode = 3 linear, R, G, B, start, end, extra, extra)`.

- Each room stores its own near and far fog packed in the cell (+0x1D8 / +0x1E2: RGB bytes, 16-bit start/end,
  flag bits 4/8 = ×scale) — written from FOG / FOG_FAR lights by 0x15560 / 0x155A0 (`Fog_PackRoom` 0x149F0).
- Section fog (FOG_BLK / FOG_FAR_BLK) goes to slot 0 by `FogMgr_SetNear` 0x14C50 / `FogMgr_SetFar` 0x14C90.
- **`FogMgr_Update` 0x15450** each frame unpacks the room's fog (0x14AC0) and moves the current fog **10 % of the
  way** toward it (`Fog_Lerp` 0x14CD0, with a faster step when the distance changes a lot). This is why fog
  changes smoothly between rooms.
- **`FogMgr_Apply` 0x14BE0** (called by 0x32870 before the world draws; argument picks near or far set) sets
  the fog colour (0x190820 → `D3DDevice_SetRenderState_FogColor`), mode/start/end (0x190750) and enables fog
  (0x190870). Per-draw on/off is 0x190690. The fog factor itself comes out of the game's vertex shaders (oFog).
- Debug page **FOG TEST** (0x155E0) edits these 10 values live: NEAR colour RGB, START, FINISH and FAR colour
  RGB, START, FINISH.

## 5. Glow filter and haze ("Glow Filter", LIGHTING_TEST)

**GLOW_MANAGER** (`GlowMgr_Create` 0x141D0, pointer 0x29CC20) is the screen-filter hub. It owns:

- render targets **0x4027** (64×64) and **0x4005** (256×256) (made with `Gfx_CreateTarget` 0x18D2D0);
- up to 64 glow requests (+0x210, 32 bytes each: model or sprite, colour, strength);
- the haze filter settings: per view 7 floats + colour, defaults far 640 (or 1,280 with flag 0x29BB54),
  480, 15, 0.8, 5, 1, 1, 1 — set from TFOG lights;
- sub-tasks `NOISE_ANIM` (0x146F0, screen noise, 4 styles) and `DIZZY` (0x148A0, wobble).

**`GlowMgr_Draw` 0x12A00** runs after the 3D scene:
1. ends the frame, renders every glow model (type 1, lit only by its glow colour) into target 0x4005
   (`GfxFilter_BeginTarget` 0x182C30);
2. blurs it (`GfxFilter_Blur` 0x182950, 0x182B00 with strength +0x74 = 15 and +0x70 = 10 steps) and adds it over
   the screen; sprite glows (type 0) go through `GfxFilter_GlowSprite` 0x181560, which reads the screen size and
   downsamples in steps until ≤ 8 pixels;
3. draws the **haze/light-shaft pass** from the TFOG values (0x1A7C40.. library: near, far, colour, wind);
4. then optional full-screen filters: 0x180680 (enabled by +0xA10), screen **fade** 0x180140 when +0x4C < 1,
   and 0x17FFF0 (repeat count +0x54).

Debug page **LIGHTING_TEST** (0x1F5D0 builds the test scene) and the glow page **FOG TEST** (0x13F10) print the
haze values with their level-file names: `nearz` LIGHT_NEAR, `farz` LIGHT_FAR, `src` LG_LENGTH, `depth`
LG_OFFSET, `dist` LG_PITCH, `d0/d1/d2` AMBIENT/DIFFUSE/SPECULAR, `pow` LG_ALPHA, `r g b`, `windX/Y/Z`.
Debug switch **"Glow Filter"** is bit 20 and **"Glow Filter Min"** has its own entry (0x242670); their readers
were not found yet (no getter function; see section 7).

## 6. Shadows (summary; details in effects.md)

`SHADOW MANAGER` (0x304D0) makes four shadow targets: **0x4001, 0x4002 (512×512), 0x4003, 0x4004 (256×256)** and
three light-space boxes: ±14, ±55, ±140 units, depth 0.1..500. The direction comes from SHADOW / SHADOW_BLK
lights. Drawing uses the Xbox **shadow-buffer compare** (`D3DDevice_SetRenderState_ShadowFunc` 0x204 =
GREATER on, 0x200 off: 0x1847E0, 0x183370, 0x184600).

## 7. Still open (static)

- Blink patterns `LIGHT_BL_TYPE` 1..4 (0x306F0 turns time into a factor; the curve shapes are not read yet).
- Who reads debug switches "Glow Filter" (bit 20) and "EffectObj Debug Disp" (bit 5).
- The haze library at 0x1A6A60..0x1A7EF0 (what exactly it draws).
- Exact texture formats of the targets (wrapper format codes 3 and 6 in `Gfx_CreateTarget`).
