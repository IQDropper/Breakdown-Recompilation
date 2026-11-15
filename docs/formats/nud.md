# Models (NUDX, pak kind 1)

Status: decoded and checked, 2026-11-14 (task P4.5). Found by reading the game's own load and draw code
(no game run) and checked on every model in the game. Tools: `tools/nud.py` (read / list / check),
`tools/model_gltf.py` (export to glTF for Blender, with textures and skeleton).

## The short version

- A model is a **NUDX** block inside a pak entry of kind 1. One entry holds up to **8 LOD slots**; most
  models have one shape, about half also have a separate shape in slot 7.
- A NUDX has **parts**. Each part = one vertex buffer + one NV2A push buffer of triangle strips + a
  **material** (render state, colours, up to 4 texture layers) + a **bone list** for skinning.
- A material names its texture by a **global texture id** (GIDX, `0x10000000 | n`). The textures are not
  inside the model: they are TIM2 pictures in the area's `.nut` file or in a pak's texture entries, and each
  picture carries the same id in a `GIDX` tag. All but 3 of the texture references in normal LODs are found
  this way; slot 7 shapes name textures the game does not ship (295 references).
- Area **scenery** is the same format, marked by byte 3 of the pak entry (`obj_in_<area>_..._M_PART`).
- Skinned vertices carry 4 weights + 4 indices into the part's bone list; the bone list holds **joint
  numbers** of the body rig (see `skeleton_animation.md`), plus a few extra numbers explained below.

Proof: Derrick, T'Lan (model 1), Alex, a soldier, a scenery room and a whole area section exported with
`model_gltf.py` look right with their own textures in a three.js viewer, and the animations play on them.
Screenshots are in the kit (`reports/P4.5_models/`), not in git (they show game art).

## Where models live

| What | Where |
|---|---|
| Characters, props, weapons | kind 1 entries in `common/base.pak`, `area/<area>/<area>.pak`, `<area>_<X>.pak` |
| Area scenery (rooms, corridors) | kind 1 entries with entry byte 3 = 1, in the section pak (`hon_under_A.pak`) |
| Collision of the scenery | entries with byte 3 = 1 but no LOD (all LOD slots −1), mostly in the area pak (`axis.pak`) |
| Textures | `<area>_<X>.nut` (one TIM2 with all of that section's textures), and kind 2 entries in paks (`BASE_TEXTURE_PAK` in base.pak, `nut_...`) |

Counts over the whole game (`python tools/nud.py game_files/romdata --check`): 2,210 models with a shape,
194 scenery entries that are collision only, 10,204 parts, 0 read errors.

Ids: object ids are unique game-wide; **scenery ids start again at 4096 in every area**, so look scenery up in
its own pak (`--pak`, or `--area hon_under_A`).

## Pak entry (0x80 bytes) for a model

The common part (`BD`, kind, id, size, offset, name at +0x14) is in `docs/decomp/engine_core.md`. Model
entries also use:

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| +0x02 | u8 | kind = 1 | `Data_SetupModelBuffers` 0x1AA10 |
| +0x03 | u8 | 1 = area scenery (`obj_in_...`), 0 = object | all entries |
| +0x10 | u32 | data offset in the pak (= +0x48) | |
| +0x40 | 4 | `OBJ\0` | |
| +0x4C | u32 | offset of the hit data (`HD51`, collision) in the pak | `Data_GetModelHitData` 0x1A5B0 |
| +0x50 | u32 | size of the model data (NUDX part) | |
| +0x54 | u32 | size of the hit data | |
| +0x58 | 8 × i32 | **LOD slots**: offset of a NUDX from the entry data, −1 = none. The loader replaces them with buffer handles | 0x1AA10, `Data_GetModelLod` 0x1A570 |
| +0x78 | i32 | ≥ 0 = the model has hit data (HD51), −1 = none | 0x1A5B0 |
| +0x7C | i32 | offset of a second hit block, added to the first | `Data_GetModelHitDataB` 0x1A5F0 |

The `HD51` hit data (chunks `INFO`, ...) is not decoded yet.

### LOD slots

The game asks for a LOD by number 0–7 (`Data_GetModelLod`, "makino: Lod Num Error"). The normal draw path
picks **0, 1 or 2 by the squared distance to the camera** (`Model_PickLodByDistance` 0x1FA90); other code asks
for slot 6 or 7 directly. Patterns in the data:

| Slots (same letter = same NUDX) | Models | Example |
|---|---|---|
| `AAAAAAAA` (one shape) | 1,272 | most props |
| `AAAAAAAB` (own slot 7) | 1,034 | `obj_obj_emergencyexit01` |
| `ABBBBBBB` | 47 | soldier body (near / far) |
| `AAAAABBB` | 25 | researcher corpse |
| other mixes with 3–4 shapes | 26 | MP5, helicopter |

Slot 7 is a special shape: 295 of the 298 texture references that cannot be found belong to slot 7 shapes, so the game
draws them without those textures (likely a shadow or reflection pass; not traced).

## NUDX header (0x20 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | 4 | `NUDX` |
| 0x04 | u32 | size of this NUDX |
| 0x08 | u16 | 1 (version) |
| 0x0A | u16 | part count |
| 0x10 | 4 × f32 | bounding sphere x, y, z, radius |

Then `part count` part headers of 0xA0 bytes, then the parts' chunk data.

## Part header (0xA0 bytes)

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| 0x00 | i32 | offset of the part's chunks from the NUDX start. **Negative** = same geometry as an earlier part (the loader stores the link) | `Model_LinkSharedPart` 0x19A8C0, 0x18E020 |
| 0x04 | u32 | size of the chunk data | |
| 0x08 | u16 | shading mode, low 9 bits: 1, 2 (skinned), 3, 5 (objects), 6 (scenery), 7, 0x101, 0x103, 0x105, 0x10D; bits 0x6000 / 0x8000 extra options | `Part_SetShadeMode` 0x19A550 |
| 0x0A | u16 | bit 15 = **multi-layer material** (use all 4 texture layers); bit 13 = an extra option of that path | `Part_SetMaterial` 0x19A760 |
| 0x0C | u32 | flags: 4 = cull back faces (else both sides), 8 = opaque pass (set by the loader), 0x800 = edge-antialias outline, 0x8000 = depth bias from +0x84, 0x20000 = no depth write, 0x40000 = fade by distance (bytes +0x8A..+0x8E) | 0x19A360, 0x19A3B0, 0x19A920, 0x199DD0 |
| 0x10 | 4 × f32 | bounding sphere of the part | |
| 0x20 | 16 × f32 | Direct3D material: diffuse RGBA, ambient RGBA, specular RGBA, emissive RGBA (mostly 1,1,1,1 / 0.8 grey) | `Part_SetMaterial` |
| 0x60 | f32 | specular power (0, 8, 20, 56) | |
| 0x64 | u16 | blend source factor (GL numbers: 1 ONE, 0x302 SRC_ALPHA, ...) | `Part_SetBlendAlpha` 0x19A3B0 |
| 0x66 | u16 | blend destination factor (0x303 ONE_MINUS_SRC_ALPHA, 1 ONE, ...) | same |
| 0x68 | u16 | blend equation: 0 = blending off, 0x8006 ADD, 0x800B REVERSE_SUBTRACT | same |
| 0x6A | u16 | alpha test reference | same |
| 0x6C | u16 | alpha test function (0x205 NOTEQUAL, 0x207 ALWAYS; < 0x200 = test off) | same |
| 0x6E | u16 | depth test function (0x203 LEQUAL; 0 = no depth test) | `Part_SetDepthCull` 0x19A360 |
| 0x70 | 4 × 8 | **texture layers**: u32 texture id (GIDX, −1 = none), u32 parameters | `Mat_BindTexture` 0x197430, `Mat_BindLayers` 0x197570 |
| 0x90 | 16 × u8 | **bone list**: joint numbers, 0xFF = end | `Part_UploadBonePalette` 0x199B60 |

Render states seen (+0x64..+0x6E): opaque `1,0,0,0,ALWAYS,LEQUAL` (7,241 parts), alpha blend
`SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ADD` with alpha test NOTEQUAL 0 (2,175), multiply-like `0, SRC_ALPHA` (398),
additive `SRC_ALPHA, ONE` (353), a few reverse-subtract.

### Texture layers

Without +0x0A bit 15 only layer 0 is bound. With it, up to 4 layers are bound to texture stages 0–3.
Layer parameters (u32): low 16 bits = sampler state (`0x00C9`, `0x00DB`, ...), **bits 10–11 = which UV set**
(`Mat_SetTexCoordIndex` 0x197330), **bits 12–15 = layer operation** for the pixel shader (`Mat_BindLayers`
builds the shader key from them), upper 16 bits = more options (`0x0212`, `0x0012`). Layer 1 on T'Lan and on hair
uses operation 2 with sampler `0xDB` (a second, probably shiny/detail map).

Texture ids: `0x10000000 | n` = a GIDX texture (`Tex_FindByGidx` 0x196D70); −16, −15, −14 = special textures
the renderer makes at run time; −1 = none.

### How a texture id finds its picture

The TIM2 picture header (0x80 bytes, see `tools/tex_extract.py`) has an extension after the 0x30 standard
bytes: at +0x30 (or +0x50 when a mipmap size table is at +0x40) the tag `eXt\0`, then `GIDX`, size 0x10, and
the id. The `.nut` file of a section and pak texture entries are lists of such pictures. The game registers the
textures of every loaded pak (`Data_SetupTextures` 0x1A870); a model then finds its texture by id among
whatever is loaded.

`TexIndex` in `nud.py` builds the same table over the whole disc (4,228 ids, cached in %TEMP%). An id can
appear in many files (shared textures are copied into each section's `.nut`); a few ids have different pictures
in different files, so the exporter takes the copy closest to the model: same file, same section's `.nut`, same
area, `common/`, then anything. Exported textures are named like the texture dumps
(`<hash>_<w>x<h>_<fmt>.png`), so a replacement texture pack matches them by name.

## Chunks (part data)

Each part's data is a list of chunks, 16-byte aligned. Chunk header: u32 type, u32 size in 16-byte units
(header included), u32, u32 count. The list ends with type `0xFFFF`. Every part in the game has exactly one
vertex chunk, one push-buffer chunk and the end chunk.

| Type | Meaning | Code |
|---|---|---|
| `0x20xx` | vertex buffer, `xx` = vertex format; two 16-byte headers then `count` vertices | `Mesh_ReadChunk` 0x19B7E0, `Mesh_RegisterVertexBuffer` 0x19B600 |
| `0x3001` | NV2A push buffer: a D3DPushBuffer at +0x10 (size at +0x1C), commands from +0x30 | `Mesh_DrawChunk` 0x19BB30 → `D3DDevice_RunPushBuffer` |
| `0x2101`, `0x2201..3` | index buffer / draw lists (code exists, no file uses them) | 0x19B6D0 |
| `0xFFFF` | end | |

### Vertex formats

The format byte is a set of bits. Fields come in this order; strides are what the files hold.

| Bit | Field | Bytes |
|---|---|---|
| — | position, 3 × f32 | 12 |
| 0x01 | normal, 3 × i16 (32767 = 1.0) + 2 bytes padding | 8 |
| 0x02 | colour, B G R A bytes (0–255 = 0–1; used for baked light on scenery) | 4 |
| 0x04 | UV set 0, 2 × f32 | 8 |
| 0x10 | UV set 1, 2 × f32 | 8 |
| 0x08 | skin: 4 × i16 weights (32767 = 1.0, add up to 32767) + 4 × u16 indices into the bone list | 16 |

| Format | Fields | Stride | Parts |
|---|---|---|---|
| 0x07 | normal, colour, UV | 32 | 4,834 (objects, scenery) |
| 0x05 | normal, UV | 28 | 2,407 |
| 0x0D | normal, UV, skin | 44 | 1,174 (characters) |
| 0x0F | normal, colour, UV, skin | 48 | 948 |
| 0x06 | colour, UV | 24 | 740 (scenery) |
| 0x0E | colour, UV, skin | 40 | 63 |
| 0x17 | normal, colour, UV, UV2 | 40 | 26 |
| 0x04 | UV only | 20 | 9 |
| 0x16 | colour, UV, UV2 | 32 | 2 |
| 0x03 | normal, colour | 24 | 1 |

### Indices (push buffer)

The push buffer is raw NV2A commands: `0x17FC` BEGIN_END (primitive; 0 = end), `0x1800` ARRAY_ELEMENT16
(two 16-bit indices per word), `0x1808` ARRAY_ELEMENT32, `0x1810` DRAW_ARRAYS (start + count−1 in the top
byte). Every part in the game uses one **triangle strip** (primitive 6), with degenerate triangles joining
strips. The reader also handles lists, fans and quads.

## Skinning

- A skinned vertex (format bit 0x08) has 4 weights and 4 indices; index `i` means `bone list[i]` of its part,
  and the bone list holds **joint numbers**: joint `j` = body track `j + 1` (joint 0 = hips).
- **Joints 22 and 23** in body models (Alex, T'Lan, soldiers) sit at the left and right wrist: the arm skin blends
  from the forearm (joint 7 / 11) to 100 % joint 22 / 23 at the wrist. In the animations joint 22 is the unused
  `extra` track, so the game must put the hand matrices there. The exporter maps 22 → `hand_L`, 23 → `hand_R`
  (seen in the data, not traced in code). Without it T'Lan's wrist bands stay behind in the rest pose.
- **Hand models** (`..._hand_l_skin`, `..._hand_r_skin`) have their own joint list: an arm joint or two,
  the palm, 15 finger joints (the hand rig `INITIAL_HAND_*`, 5 fingers × 3), and a forearm joint. The numbering
  shifts by character (Derrick: palm = 2, fingers 3–17, forearm 18, upper arm 0; Alex: palm = 1, fingers 2–16,
  forearm 17). The exporter puts each joint on upper arm / forearm / hand by where its vertices sit along the arm.
- **Hair** (`..._hair_...`) and **Alex's head** (`obj_navi_head_skin`, joints up to 28 = a face rig,
  `INITIAL_HEAD_NAVI`) use their own rigs; the exporter fixes them to the head bone.
- Derrick's own head is part of his body model and uses body joints 2, 3, 4, 5, 9.
- Which body size (INITIAL_TRANS A1 / A2 / CHILD) a character uses is set at run time. A1 fits Derrick, Alex,
  T'Lan and the soldiers best (vertices closest to their bones), so the exporter uses A1 by default (`--trans`).

## Units and axes

Positions are game units, 10 units ≈ 1 m (Derrick is 16.6 units tall). Y is up, characters face +Z. Xbox
Direct3D is left-handed: the exporter mirrors X (so the front stays +Z, glTF's front), scales by 0.1 and reverses
the triangle winding, all baked into the data.

## Exporting (tools/model_gltf.py)

```
python tools/model_gltf.py game_files/romdata 8,5,6,7 %TEMP%/derrick/derrick.gltf --anim 1794
python tools/model_gltf.py game_files/romdata 9,14,11,15,12,13 %TEMP%/alex/alex.gltf
python tools/model_gltf.py game_files/romdata 1 %TEMP%/tlan/tlan.gltf --anim 7870
python tools/model_gltf.py game_files/romdata --area hon_under_A %TEMP%/hon_under_A/area.gltf
python tools/model_gltf.py game_files/romdata 4099 %TEMP%/room/room.gltf --pak game_files/romdata/area/hon_under/hon_under_A.pak
```

Output: `.gltf` + `.bin` + one PNG per texture in the same folder (Blender: File → Import → glTF 2.0). Every
material keeps the game values in `extras` (texture ids and parameters, blend, alpha test, shade mode, flags,
colours); extra texture layers are written as PNG and listed there too. Output is game art: write it under
%TEMP% or the kit's reports, never into git.

Character = several models: Derrick 8 (body) + 5 (hair) + 6 / 7 (arms and hands); Alex 9 (body) + 14 (head)
+ 11 (hair) + 15 (eyelashes) + 12 / 13 (hands); T'Lan 1 (one model).

## Checks done

- `python tools/nud.py game_files/romdata --check`: every LOD of every model reads; all indices in range;
  texture ids found for all but 3 references outside slot 7.
- Exports viewed in three.js (r160): Derrick (rest pose, rider kick 1794), T'Lan (rest, kick 7870), Alex (body,
  head, hair, eyelashes, hands), the monitoring room of hon_under A (4099), a prop (2124), all scenery of
  hon_under A. Textures, alpha, culling and scale look right; no loader warnings.
- Block-mode animations (header flag 8): the sampler (`0x193970` → `0x192C40`) reads exactly the addresses
  `anim_dump.py` reads. Over all 956 block-mode files the pose change at a block start (body: 1,006 units ≈ 5.5°)
  equals the change inside a block (982), so blocks join cleanly. Viewed: 4623 (Derrick, food poisoning), 6651
  (Alex, "my fate"), 2028 (blown back, one key per frame), 1518 (thrown, step 1), 2084 (Alex falling).
  The last key of some cutscene animations jumps (2084: 2.7 m at the final frame): the data stores it twice and
  both copies agree, so it is a cut in the scene, not a reading error.

## Still open

- Hit data (`HD51` blocks, `INFO` chunk): collision of scenery and objects.
- Exact use of LOD slot 7 (and 6); the two distance limits of `Model_PickLodByDistance`.
- Hair, face and finger rigs: exported fixed to the head / hand, not as their own bones.
- Where objects and scenery pieces are placed (the `.evd` event files); scenery vertices are already in area
  space, objects export at their own origin.
- Layer operations (bits 12–15) and the sampler bits (wrap / clamp, filters) in detail; shade modes in detail.
- The 3 normal-LOD texture ids not found on the disc.

## Code map (default.xbe)

| Address | Role |
|---|---|
| `0x1AA10` | `Data_SetupModelBuffers`: per model entry, registers each LOD slot (`Model_Register`) |
| `0x1A570` / `0x1A5B0` / `0x1A5F0` | LOD handle / hit data / second hit block of a model id |
| `0x18E2B0` → `0x18DD70` → `0x18DB60` | register a NUDX from memory: copy header + parts, link shared parts, copy chunks |
| `0x18E020`, `0x199C80` | same from a file (chunk by chunk) |
| `0x19A8C0` | link a part that shares an earlier part's geometry |
| `0x18D5A0` | draw a model: every part through `0x19AA20` / `0x19AB80` |
| `0x199DD0` | distance fade (flag 0x40000) |
| `0x199B60` | bone list → vertex shader matrices |
| `0x19A760` | part material: `0x19A360` depth/cull, `0x19A3B0` blend/alpha, `0x19A550` shade mode, textures |
| `0x197430` / `0x197570` / `0x197330` | bind one texture / up to 4 layers / UV set per layer |
| `0x196D70` | texture id → texture |
| `0x19A920` → `0x199B30` → `0x19BB30` | run the part's chunks (push buffer) |
| `0x1FA90`, `0x1FB10`, `0x1FBA0` | pick LOD by distance, draw a model by id |
| `0x1DE40` | debug screen: model name and its 8 LOD slots |
