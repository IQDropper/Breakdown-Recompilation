# Collision (hit data, `HD51`)

Status: decoded and checked, 2026-11-14 (task P4.5a). Found from the files and the game's own reader
(no game run). Tools: `tools/nud.py` (`--hit`, `--hit-area`, `--check-hit`), `tools/model_gltf.py`
(`--collision`, `--collision-only`).

## The short version

- Collision is a separate, simpler mesh: a list of **points**, a list of **triangles** (3 point numbers + a
  **surface word**), a **tree** over the triangles for fast lookups, and a few **edges**.
- It is stored as an `HD51` block, found through a model's pak entry (see `nud.md`, entry +0x4C / +0x78 / +0x7C).
- **Scenery collision** sits in the **area pak** (`hon_under.pak`), in entries with no LOD, named like the section's
  scenery without the section letter: section A's `obj_in_hon_under_A_kansi_M_PART_HO` (looks) goes with
  `obj_in_hon_under_kansi_M_PART_HO` (collision). The points are in area space, like the scenery.
- **Objects** (doors, props, the helicopter) carry their own hit data in model space, sometimes two blocks.
- Every one of the 4,310 HD51 blocks in the game reads with this layout: 1,300,207 triangles, 1,580,808 points,
  648 edges, 0 problems (`python tools/nud.py game_files/romdata --check-hit`).

Proof: the collision of 7F / Main Research Center section A (hon_under_A) exported as a see-through overlay sits on
the visible floors, walls, desks and cabinets (screenshots in the kit, `reports/P4.5a_collision/`, not in git). In
numbers: of the 3,132 collision triangles of that section, the centre of 58.6 % lies on a visible surface (within
0.05 units = 5 mm), 84.5 % within 10 cm, 97.4 % within 50 cm; median distance 1 mm. The rest are the simplified
boxes around furniture and a few blockers that have no visible surface (see type 2 below).

## Finding the block

`Data_GetModelHitData` (0x1A5B0): if pak entry +0x78 ≥ 0, the block is at **entry +0x4C − entry +0x10** bytes
from the entry's data, which is the same as **pak file offset +0x4C** (the loader puts the entry data at +0x10).
`Data_GetModelHitDataB` (0x1A5F0): a second block +0x7C bytes after the first, when +0x7C > 0 (604 entries, e.g. the
photo `obj_s02a03_010_photo_HM`, 2 blocks of 2 triangles). Entry +0x54 is the size of all hit data.

## Block layout

All numbers little-endian. A block is a chunk with children; every chunk is:

| Off | Type | Meaning |
|---|---|---|
| 0x00 | 4 chars | name (`HD51`, `INFO`, `BSP `, `TRI `, `AEG `, `PNT `) |
| 0x04 | u32 | size of the data that follows; **bit 0 = has child chunks**, size = value & ~1 |
| 0x08 | | data |

`HD51` has bit 0 set; its data is the list of child chunks, always in the order INFO, BSP, TRI, AEG, PNT. The game
walks the list (`Hit_WalkChunks` 0x424D0) and picks the chunks by name (`Hit_ReadChunk` 0x42540), so the order does
not matter to it.

### INFO (0x28 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | 1 in every block (copied to the hit view +0x0C, use not traced) |
| 0x04 | u32 | 1 in every block (hit view +0x10) |
| 0x08 | 3 × f32 | box min x, y, z |
| 0x14 | f32 | 1.0 |
| 0x18 | 3 × f32 | box max x, y, z |
| 0x24 | f32 | 1.0 |

`Hit_GetBox` (0x426E0) returns the two corners; `Hit_GetBoxCentre` (0x39F50) their middle.

### PNT: points

`count = size / 12` points of 3 × f32 (x, y, z). Same units and axes as the models (10 units ≈ 1 m, Y up,
left-handed). Some blocks pad the chunk to 16 bytes, so use `size // 12`.

### TRI: triangles (8 bytes each)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | 3 × u16 | point numbers |
| 0x06 | u16 | **surface word** |

Surface word, as `Hit_SkipTriangle` (0x42750) reads it against the query's flags (`mask`):

| Bits | Meaning |
|---|---|
| 0–3 | **type**. 0 = solid (1,262,604 triangles). 2 = only hit by queries with flag 0x1 (31,303; e.g. the doorways of section A, which have no visible surface, so likely a blocker for one kind of mover). 3 = skipped by queries with flag 0x8 (608). 15 = never hit by this test (412). 1 (4,044) and 4 (1,236) = like solid; type 4 with material 34 is always hit. |
| 4–10 | **material** number 0–127 (0–44 used). 34 is skipped by queries with flag 0x10 or 0x100, 35 by queries with flag 0x100. The others do not change the hit test; they are likely the footstep / bullet-mark sound and effect kind (not traced). Most common: 3 (342k), 1 (241k), 10 (216k), 0 (143k), 35 (102k). |
| 11–15 | more flags (values 0, 1, 4, 6, 7, 8, 10, 12, 15, 16 seen); not used by the filter. |

Winding is not consistent (floors face both ways), so the exporter draws collision double-sided.

### BSP: tree over the triangles

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | 4 in every block: the nodes start this many bytes in |
| 0x04 | n × 8 | nodes, `n = (size − 4) / 8`, node 0 = root |

Node (8 bytes): i16 **front child**, i16 **back child** (−1 = none), u16 **first triangle**, u16 **triangle
count**. Every node owns a run of TRI entries; together the runs cover TRI exactly once, in the order of a
depth-first walk (root, front side, back side). `Hit_WalkTree` (0x457E0) tests a node's triangles, then visits the
front and back children. The split plane is not stored: the code builds a plane from the node's first triangle.
The node's triangles are mostly on that plane, but not always, and the children are not strictly on one side of it,
so the exact split rule is still open (the tree is not needed to read the mesh).

### AEG: edges

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u16 | edge count (non-zero in 119 of the 4,310 blocks, up to 12) |
| 0x02 | count × 8 | edge: u16 point, u16 point (the edge), u16 a third point, u16 flags (3, sometimes 2) |

Found in many areas (bettou, silo, axis, future ...), along the rim of a shape (the third point is the next point of the
outline). Likely grab or step-up ledges; not traced. The game keeps the count at hit view +0x30 and the list at +0x34.

## The hit view (game memory)

`Hit_Open` (0x42640) fills a small struct from a block: +0x00 PNT, +0x04 TRI, +0x08 BSP (also +0x2C), +0x0C / +0x10
the two INFO words, +0x14 box min, +0x20 box max, +0x30 edge count, +0x34 edges. If the data does not start with
`HD51` it reads another, older layout with offsets at +8 / +0xC / +0x10 (no file in the game uses it).

## Tools

```
python tools/nud.py game_files/romdata --hit-area hon_under_A          # the section's collision, per piece
python tools/nud.py game_files/romdata --hit 2362                      # an object's hit blocks
python tools/nud.py game_files/romdata --check-hit                     # every block in the game
python tools/model_gltf.py game_files/romdata --area hon_under_A %TEMP%/hon_under_A/hit.gltf --collision
python tools/model_gltf.py game_files/romdata --area hon_under_A %TEMP%/hon_under_A/hit_only.gltf --collision-only
python tools/model_gltf.py game_files/romdata 150 %TEMP%/door/door.gltf --collision
```

The export puts each collision piece in its own mesh named `<entry name> [hit]`, one primitive per triangle type,
see-through and double-sided: solid = orange, type 2 = blue, type 3 = green, types 1 / 4 = purple, type 15 = grey,
edges as red lines. The primitive's `extras` list the materials and surface words. In Blender: hide or show the
`[hit]` objects to compare with the scenery.

## Still open

- Which query sets which flag (0x1, 0x8, 0x10, 0x100): who walks into type 2 blockers, who ignores material 34 / 35.
- The meaning of the material numbers (sound / effect table) and of bits 11–15.
- The split rule of the BSP tree; the two INFO words.
- What the AEG edges are for.

## Code map (default.xbe)

| Address | Role |
|---|---|
| `0x1A5B0` / `0x1A5F0` | `Data_GetModelHitData` / `...B`: block of a model id |
| `0x42640` | `Hit_Open`: block → hit view |
| `0x424D0` / `0x42540` | walk the chunks / read one chunk by name |
| `0x426E0`, `0x39F50` | box corners / box centre |
| `0x42750` | `Hit_SkipTriangle`: surface word against query flags |
| `0x427D0` | `Hit_TestTriangle`: one triangle against a moving sphere |
| `0x457E0` | `Hit_WalkTree`: recursive tree walk |
| `0x46190` | `Hit_ForEachTriangle`: callback per triangle with its 3 points and surface word |
