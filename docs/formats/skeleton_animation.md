# Skeletons and animations (pak kind 0)

Status: decoded and checked, 2026-11-14 (task V1.1). Found by reading the game's own animation code
(no game run). Tools: `tools/anim_dump.py` (dump), `tools/anim_gltf.py` (export to glTF).

## The short version

- There is **no separate skeleton file**. The game keeps everything as "animations" (pak kind 0, names `gym_...`).
- An animation is a list of **tracks**. Each track is one step in a chain of transforms: a move (translation),
  a turn (rotation) or a resize (scale).
- How the tracks connect (which bone hangs off which) is a small **chain table** inside every animation.
- Bone **lengths** are not in the moves. They come from a second, "initial" animation that only holds
  translations: `gym_INITIAL_TRANS_A1` (id 1101) for the 24-track human body.
- The **rest pose** the models are built in is `gym_INITIAL_TRANS_A1` + `gym_INITIAL_ROT_A` (id 1100).
- A **model** (NUDX) names its bones by **joint number** (track number − 1). Joint 0 = track 1 = hips.
- Keys are whole poses, one every 1–3 game frames. Rotations are 3 × 16-bit angles. Nothing is compressed beyond
  "store constant tracks once" and an optional per-track block scheme.

Proof it works: Derrick's body (model 8) + rest pose + the rider kick (anim 1794) exported to glTF plays correctly
in a three.js viewer (wind-up, flying kick, crouched landing). T'Lan's right kick (7870) also plays on the same rig.
The frame strips are in the kit (`reports/V1.1_anim/`), not in git (they show game art).

## Where things live

| What | Where | Id / name |
|---|---|---|
| Body rest offsets (bone lengths) | `common/base.pak` | 1101 `gym_INITIAL_TRANS_A1` (adult), 1102 `_A2`, 4960 `_CHILD` |
| Body rest rotations | `common/base.pak` | 1100 `gym_INITIAL_ROT_A` |
| Hands (15 tracks) | `common/base.pak` | 1093/1096 `INITIAL_HAND_L/R_ROT_A`, 1094/1097 `..._TRANS_A1` |
| Heads / faces | `common/base.pak` | 1099 `INITIAL_HEAD_NAVI`, 1188 `INITIAL_HEAD_X`, 4965 `..._CHILD` |
| Hair, flags, props | various | `INITIAL_TRANS_NAVI_HAIR`, `INITIAL_FLAG_*`, `CT_INITIAL_*`, ... |
| Derrick third-person body | `common/base.pak` | model 8 `obj_me_tp_skin_shimono_a` |
| Rider kick | `common/base.pak` | anim 1794 `gym_H_FN_NN_BD_riderkick_SW` (106 frames) |
| T'Lan right kick | `area/axis/axis_B.pak`, `area/labo2/labo2_I.pak` | anim 7870 `gym_NT_FT_NN_BD_Rkick_SR` (151 frames) |

The game picks the rest-offset animation per character: it lives at character struct +0x14 (anim id) and is
passed as the "reference" to the pose code (see `0x57620`, which also checks both have 24 tracks).

## Animation header (all little-endian)

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| 0x00 | u16 | flags: 2 = some tracks have no data, 4 = "delta" keys, 8 = block keys, 0x100 = curves (unused) | `0x192CA0`, `0x192D40`, `0x193970` |
| 0x02 | u16 | track count (24 = human body; ≤ 0x80) | loops in `0x193580`, `0x194040` |
| 0x04 | u16 | frame count (game frames) | `0x106190` returns it as the length |
| 0x06 | u16 | size of each key after the first (flag 4) | `0x192D40` |
| 0x08 | f32 | 4.4 in every file — unknown | — |
| 0x0C | u32 | top 4 bits = key step (0/1 = every frame, 2, 3). Step > 5 would mean packed rotations (never used) | `0x193970`, `0x192B90` |
| 0x10 | u32 | 0 | — |
| 0x14 | u32 | tracks × 0x80 (working memory size) | all files |
| 0x18 | u32 | tracks × frames × 12 (size if fully unpacked) | all files |
| 0x1C | u32 | number of u16 entries in the chain table | all files |
| 0x20 | u32 | offset of the track table | `0x192C10` |
| 0x24 | u32 | offset of the chain table | `0x192C10` |
| 0x28 | u32 | offset of the key data | `0x192C10` |
| 0x2C | u32 | size of one full key | `0x192CA0` |
| 0x30 | — | 16 more bytes (e.g. `ffff 1700` + a float); not read by the pose code | — |

All offsets are from the start of the animation. `0x192C10` builds a 5-pointer handle: base, base+0x20,
track table, chain table, key data.

### Track table (16 bytes per track)

| Off | Type | Meaning |
|---|---|---|
| 0 | i16 | parent, as an editor hint (tracks 0 and 1 count as one node); the game uses the chain table instead |
| 2 | i16 | −1, or 0 on track 1 ("same node as track 0") |
| 4 | u8 | track type (below) |
| 5 | u8 | 1 = has children, 0 = end of a limb |
| 6 | u8 | **joint number** (what the models use; track 0 and 1 are both joint 0) |
| 7 | u8 | flags: 2 = no data (only if header flag 2), 4 = constant (only stored in key 0, header flag 4) |
| 8 | u32 | offset inside a "delta" key (flag 4) |
| 12 | u32 | offset inside a full key |

### Track types and data sizes

Sizes match the game's table at `0x27C580`: `4 4 4 12 2 2 2 6 4 4 4 12 12 12 12 4`.

| Type | Data | Use |
|---|---|---|
| 0, 1, 2 | f32 | scale X / Y / Z |
| 3 | 3 × f32 | scale XYZ |
| 4, 5, 6 | i16 | rotate X / Y / Z |
| **7** | 3 × i16 | **rotate XYZ** (every body joint) |
| 8, 9, 10 | f32 | move X / Y / Z |
| **0xB** | 3 × f32 | **move XYZ** (track 0 = root, and every track of an INITIAL_TRANS file) |
| 0xC–0xE | 3 × f32 | camera animations only, meaning open |
| 0xF | f32 | camera animations only, looks like field of view (≈1.14) |

**Angles:** int16, `angle_rad = value × π / 32768` (the constant at `0x20C630` is the double π/32768).
Interpolation (`0x193170`) takes the shorter way round, also trying the equal pose (x+180°, 180°−y, z+180°).

### Chain table (hierarchy)

A list of i16 track numbers: each run is a path from the root down to the end of a limb, `-1` ends a run,
`-2` ends the table. The matrix code (`0x194040`) walks it with a matrix stack: reset at each `-1`, apply the
track, store the result as that track's matrix (64 bytes per track). Parent of a track = the number before it.

Human body (24 tracks), names are ours:

```
0 root_move  1 hips  2 spine  3 chest  4 neck  5 head
3 -> 6 clavicle_L  7 upperarm_L  8 forearm_L  9 hand_L
3 -> 10 clavicle_R 11 upperarm_R 12 forearm_R 13 hand_R
1 -> 14 pelvis -> 15 thigh_L 16 calf_L 17 foot_L 18 toe_L
           14 -> 19 thigh_R 20 calf_R 21 foot_R 22 toe_R
23 extra (no parent; constant zero in the body files, probably an attachment point)
```

### Building one bone's matrix (`0x194040`, `0x1933C0`)

For each track in chain order, starting from the parent's matrix:

- move (0xB / 8–10): translate.
- rotate XYZ (7): translate by the **reference** track's move (INITIAL_TRANS, same track number), then
  rotate Z, then Y, then X on the stack. In column-vector terms: `M = Parent · T · Rz · Ry · Rx`.
- scale: scale.

So a body animation only stores the root position (track 0) and the 23 joint turns; bone lengths always come from
the character's INITIAL_TRANS file. This is why one animation can drive different body sizes (adult, child).

**Root quirk:** INITIAL_TRANS_A1 gives both track 0 and track 1 the move (0, 10.35, 0), so the game's joints sit
10.35 units above where the mesh is. Skinning undoes it (mesh = current × inverse(rest)), so nothing shows.
`anim_gltf.py` drops the double offset so the bones sit inside the mesh; the picture is the same.

### Key data

Keys are full poses. Key `k` is game frame `k × step`; when `(frames−1)` is not a multiple of `step` there is one
extra last key at the final frame. Keys = `(frames−1)//step + 1 (+1)`. Checked: 1794 → 54 keys,
7870 → 76 keys, both ending exactly at the end of the file.

Where a track's value is (`0x192CA0` / `0x192D40`):

- plain: `keys + key_size × k + track.off_full`
- flag 4 (delta): key 0 as plain; key k > 0 at `keys + key_size + delta_size × (k−1) + track.off_delta`;
  constant tracks (track flag 4) always read key 0.
- flag 8 (block): the key data starts with a 0x40-byte header `BLOCK00`, +0x08 = 60, +0x0C = blocks per track,
  +0x10 = keys per block (30). Then one u32 per (track, block): low 24 bits = offset from the key data; top byte
  0 = one value for the whole block, non-zero = one value per key (`offset + size × (k mod 30)`) (`0x192C40`).
- flag 2: tracks with track flag 2 have no data; the rest pose is used.

Frame rate: frames are game frames. The block header holds 60 and the game logic runs at 60 per second, so
`anim_gltf.py` uses 60 fps; at that speed the rider kick lasts 1.75 s and looks natural. (Not proven from code.)

## Models: how skinning links to the skeleton

NUDX header: `'NUDX'`, +0x04 size, +0x0A u16 part count, +0x10 bounding sphere. Part headers are 0xA0 bytes from
0x20: +0x00 data offset, +0x04 size, +0x0C flags, +0x10 sphere, **+0x90 bone list** (16 bytes, `0xFF` = unused).

Skinned vertices (formats 0x0D / 0x0E / 0x0F, 44 / 40 / 48 bytes) end with 16 bytes:
4 × i16 weights (32767 = 1.0, they add up to 32767) and 4 × u16 indices into the part's bone list.

The bone list holds **joint numbers** (track byte +6), so bone `j` uses the matrix of track `j + 1`.
Checked on Derrick: the part with list `[4]` sits at head height (joint 4 = head), the head mesh's list is
`[2,3,4,5,9]` = chest, neck, head, both collarbones. With track numbers instead, the head would follow the hand.
Vertices are stored in the rest pose (model space, feet at y = 0, Derrick 16.6 units tall ≈ 1.66 m, so
**10 units ≈ 1 metre**). Rest pose check: with INITIAL_TRANS_A1 + INITIAL_ROT_A, vertices bound 100 % to one bone
land close to that bone in its own frame (e.g. upper arm vertices along +X, 0–1.4 units from the shoulder).

## Checks done

- `python tools/anim_dump.py game_files/romdata --check` → **5,051 / 5,051 animations read**, every key in range.
- 1794 and 7870 dumped as text and JSON; frame counts, key counts and file sizes add up exactly.
- Derrick + rest pose + 1794 and 7870 exported with `anim_gltf.py`, viewed in three.js: feet on the floor, no
  stretched triangles, kick poses look right.

## Still open

- Header +0x08 (always 4.4) and the 16 bytes at +0x30.
- Camera track types 0xC–0xE.
- Block mode (flag 8, 956 files): checked in a viewer and against the game's sampler in task P4.5
  (`nud.md`, "Checks done"); blocks join cleanly.
- Packed rotations (step > 5) and curve mode (flag 0x100): code read, no file uses them.
- Which INITIAL_TRANS each character uses (A1 / A2 / CHILD) is set at runtime (character +0x14), not traced yet.
- NUDX models: see `nud.md` (full layout, textures, joints 22 / 23 = hands, hand models). Export with
  `tools/model_gltf.py` (textures + skeleton + animation); `anim_gltf.py` stays as the first, minimal version.

## Code map (default.xbe)

| Address | Role |
|---|---|
| `0x1A330` | data manager lookup (kind, id) → pointer |
| `0x40CB0` / `0x40D00` | load anim by id + make handle |
| `0x192C10` | handle: base, base+0x20, tracks, chains, keys |
| `0x192CA0`, `0x192D40`, `0x192C40` | address of a track's value in a key (plain / delta / block) |
| `0x193970` | sample at time t: key, blend factor, step handling |
| `0x193580` → `0x1933C0` | fill pose for one key (with reference translations) |
| `0x1931E0` | same for the exact last frame |
| `0x193170`, `0x193100` | shortest-way angle blend |
| `0x192B90` | rotation unpack (int16 or packed) |
| `0x193900`, `0x193740`, `0x1935C0`, `0x193680` | curve mode (unused) |
| `0x194260` → `0x194040` | chain walk → one 4×4 matrix per track |
| `0x18C110/120/130`, `0x18C180`, `0x18C3B0`, `0x18C220` | matrix stack: rotX/Y/Z, translate, scale, store |
