# Story progress in the save (P4.7)

How Breakdown remembers how far you are in the story, where that sits in the save, which code reads and writes it,
and what a save must hold to start at any of the 192 checkpoints. Worked out statically (no game run) on
2026-11-14, then checked against two real saves (sandbox copies).

Tools: `tools/story_flags.py` (replays the story scripts), `tools/checkpoint_progress.json` (its result for every
checkpoint), `tools/save_checkpoint.py` (writes a test save). Function names: `config/function_names.csv`, section
"Story progress and checkpoint saves".

**In one paragraph:** story progress is a store of **2048 on/off flags** and **1000 numbers**. Scripts switch flags
on as scenes finish ("tutorial 1 done", "burger room done"...). Each chapter has a control script for Derrick whose
`FlagCheck` state walks the scene flags in story order and starts the first unfinished scene. The save keeps a copy
of the store (**numbers at 0x59C4, flags at 0x6964**). The black screen after a checkpoint jump came from a save that
still held the old area's flags and event states: the chapter script then waits for a scene that is not in this
area. The new `save_checkpoint.py` writes the right flags, scene pack and position for each checkpoint.

## 1. The store

| What | Where | Size |
|---|---|---|
| Store task | `[0x2A8E04]`, made by `Progress_Create` 0x27D20 from `Game_CreateWorldTasks` 0x227A0 | |
| Buffer | task +0x40 -> pointer; **flags at +0** (bit `i & 7` of byte `i >> 3`), **numbers at +0x100** (u32 each) | 0x100 bytes of flags = 2048 flags; 1000 numbers |
| Clear | `Progress_Clear` 0x27A70 (new game) | |

| Code | Does | Script command |
|---|---|---|
| `Progress_SetFlag` 0x27C10 (i, on) | flag i on/off | **#769 `Flag_Set`** (wrapper 0xAC640) |
| `Progress_GetFlag` 0x27C80 (i, &on) | read flag i | **#770 `Global_IsOn`** (0xAC660) |
| `Progress_SetNumber` 0x27BB0 / `Progress_GetNumber` 0x27BE0 | number i | **#759 `Global_Set` / #760 `Global_Get`** |
| `Progress_Store` 0x27B40 (numbers dst, flags dst) | store -> save buffer | |
| `Progress_Load` 0x27AD0 (numbers src, flags src) | save buffer -> store | |

The numbers are used a lot by scripts (30,000 `Global_Set` calls) but as working values: **all 1000 are 0 in every
real save seen** (opening, cp02a_02 twice, cp02a_05). Flags 2000-2005 are debug starts (only set while a pad button
is held in `Setting`); **flag 2007** is written by the save code itself (`Save_BuildCheckpoint` sets it to
`0x12AB80(0) != 0` just before copying) and read back on load (`Resume_RestoreObjects` passes it to 0x12C480).

## 2. Where it sits in the save

The save is 0x1FC50 bytes (`UDATA/4e4d0009/<slot>/data.sav`). Full layout as far as known:

| Offset | What | Written by |
|---|---|---|
| 0x04 | save type bits: 1 = area start, 3 = checkpoint (bit 1 set by `Save_BuildCheckpoint` at the end) | |
| 0x08 | checkpoint table index (`Checkpoint_FindIndex` 0x12C660 of the name) | |
| 0x0C / 0x0E | u16 counts of the two event-state lists below | `Save_RecordEvent` 0x28480 |
| 0x10 | value of 0x1125C0 (not decoded) | |
| 0x14..0x24 | play-time / counters 0x29C5E4..0x29C5F0 (0x1C/0x6B08 = save count, max 999) | |
| 0x24..0x58 | 13 dwords copied from 0x29BB1C (the same 13 go into the settings save, `Save_WriteOptions`) | |
| 0x90 / 0xB8 / 0xE0 / 0x120 | area, section, **story-scene pack**, checkpoint name | |
| 0x140 | area check value = `romdata/area/vsn[area id]` | |
| 0x144 + 12*n | **event states**: u16 event id, u16 kind, u16 mode, u16 0, u32 extra (up to 0x600) | `Save_RecordEvent` |
| 0x4944 + 0x84*n | longer records for kind-4 events (up to 0x20) | `Save_RecordEvent` |
| **0x59C4** | **story numbers**, 1000 x u32 (to 0x6964) | `Progress_Store` |
| **0x6964** | **story flags**, 0x100 bytes (to 0x6A64) | `Progress_Store` |
| 0x6A64..0x6ABC | sizes, offsets and counts of the saved-object data at 0x7C30 (0x6A70 = Derrick's block, 0x6A80 = Alex's, 0x6A98 / 0x6AA8 / 0x6AB8 = counts of saved characters / type-0x11B / type-0x102 objects) | `Save_BuildCheckpoint` |
| 0x6AC0..0x6B5F | area-start copies: vsn, index, 0x6AD0 (0x30 bytes from 0x2BB90), counters 0x6B00, section 0x6B10, scene pack 0x6B38 | `Save_BuildAreaStart` 0x28030 |
| 0x6B60 / 0x7B00 | **area-start copy of the store** (numbers / flags) | `Save_BuildAreaStart` |
| 0x7C30.. | saved-object data: first the MI_EVENT list (size at 0x6A6C, 0x1400 = 64 x 0x50, `MiEvent_Collect` 0xC8F30, empty in every save seen), then Derrick's 0x454-byte block (offset at 0x6A70, 0x18A4 in real saves): +0x8 flags, +0xC position (0x94E0), +0x18 Alex's position (0x94EC), +0x28 turn, **+0x2C u16 motion script (11 = bhb_mo_hero), +0x2E u16 AI script he is running** (810 = bhb_ai_s02_HE at cp02a_05, 989 = the burger-room scene script at cp02a_02); then Alex (size 0 so far), then saved characters / objects | `Save_BuildCheckpoint` |
| 0x1FC1C | 0xBD040119 marker + 4 values (0x12E40) | |
| 0x1FC30 | HMAC-SHA1 signature over 0..0x1FC30 (recomp key = 16 zero bytes) | `Save_WriteSlot` 0x29290 |

There are no separate inventory or power-up fields in the story block: weapons and powers are part of Derrick's
saved data at 0x7C30 (`0x3D7F0` serialises a character) and a few story flags.

## 3. Which code reads and writes it

**Saving.** A save point is an `EV_TYPE_LOAD` event with `SAVE_POINT 1` and `NAME_ID save_cpXX` in the area's
`.evd`. An `EV_TYPE_OBSERVE_AND` watches the trigger box `areahit_cpXX` (mode 2 = Derrick inside) and the save point
(mode 1 = armed) and (likely) sets the save point to mode 2. `EvSavePoint_Update` 0x370F0 then calls
`Save_BuildCheckpoint` 0x29E00 (name, SAVE_PLAYER, SAVE_NAVI). Other `OBSERVE_AND` events disarm save points as the
story moves on (mode 4: the burger room disarms cp02a_03..05). Scripts can also save directly: `Save_Checkpoint`
(#1519, 0x82940) is used for the 12 `save_cpaiXX` checkpoints. `Save_BuildCheckpoint` fills the header, runs
`Progress_Store(save+0x59C4, save+0x6964)`, copies the area / section / scene-pack names from the area task
`[0x2A8DE8]` (+0x2EC / +0x314 / +0x33C), walks every event task with `Save_RecordEvent`, and saves Derrick, Alex and
some characters and objects. `Save_WriteSlot` signs and writes it.

**Loading a checkpoint.** `Resume_CheckpointStep` 0x2A920 (a small state machine on task `[0x2A8E0C]`):
1. `Progress_Load(save+0x59C4, save+0x6964)`; counters back to 0x29C5E4.
2. Load the story-scene pack named at 0xE0 (0x2D120, then waits for 0x2DCC0).
3. Load the area at 0x90 (0x2DD50), wait 10 frames, load the section at 0xB8 (0x2DF10).
4. When the section is in: `Resume_RestoreObjects` 0x2A3F0 puts Derrick and Alex back from 0x7C30, applies flag
   2007, and `Resume_RestoreChars` / `RestoreObj11B` / `RestoreObj102` put saved characters and objects back **by
   event id**. While the level starts, events ask `Save_GetEventMode` 0x28B40 (event id) for their saved mode.

The area-start path (`Resume_Start` 0x2AC10 -> `Resume_AreaStep` 0x2A540) does the same from the copies at
0x6B60 / 0x7B00 / 0x6B38.

**Using it: the chapter control scripts.** Each chapter has a control script for Derrick, `bhb_ai_sNN_HE`, in the
area packs (s02: hon_under + bettou, s03: hon_hon (+ bettou), s04: plant, s06: shaft, kyodo1, gesui, tunnel2, s07:
park, hill, silo, tunnel2, s08: future, future2, s09: park2, hill2, silo2, axis, park3; labo1/labo2 have none).
In s02 and s06 its state `FlagCheck` runs every frame:
- `Area_GetId` (#871, the area id byte 0x29C5F5) picks the branch for this area;
- it tests the scene flags in story order with `Global_IsOn`; for the first scene not done it loads that scene's
  story pack (`Block_Find` #982 / `Block_Load` #866, e.g. `area/hon_under/block_B_s02a02_100_p`) and waits for
  Derrick to walk into the scene's trigger box (`Event_GetMode('areahit_hon_under_room3_mae') == 2`);
- then `Change_<scene>_AI` hands Derrick to the scene's own script (`AI_SetScript` #678, e.g.
  `bhb_ai_s02a02_100_tut3_HE`). That script and the other characters of the scene (`..._NV` Alex, `..._R0`
  researchers) switch the scene's flags on, and control returns to `FlagCheck`.

The `Setting` state of the same script also uses the flags to put doors, items and lights in the right state
(`cmd_850('room0_door_L', 4)` ...), so a save with the right flags rebuilds the area without its event list.
s03, s04, s07, s08 and s09 use other state layouts (`EV_Pack_check`, `NORMAL`, `s08a02`...) that are not replayed
yet.

**Scene order of chapter 2** (from the replay; flag that marks the scene done in brackets):
hon_under: wake (219), tut1+tut2 (239), tut3 (269), burger room / sykj (282), ammo (351, saves `save_cpai02a_01`),
cdky (299), ghel (328), map talk (321); bettou: gwhk (329), 1stt (339), rkrs (364), telr (369), omen (371), pow1
(379), 3tbt (389).

## 4. What each checkpoint needs

`tools/story_flags.py --json tools/checkpoint_progress.json` makes the table; `save_checkpoint.py --progress`
prints it. Per checkpoint:

| Field | How it is found |
|---|---|
| `flags` | **scene** method (chapters 2 and 6: hon_under, bettou, shaft, kyodo1, gesui, tunnel2): the flags on when the pending scene starts, from replaying `FlagCheck` (all trigger boxes reported as touched) and switching on every flag the finished scenes' scripts set with a constant 1 and never 0. **area** method (all other areas): every flag that earlier areas switch on (same rule), nothing of this area yet |
| `scene`, `block` | the pending scene and its story pack (goes to 0xE0) |
| `position` | the checkpoint's trigger box `areahit_cpXX`, Y moved down to the floor (box Y - box height + 1.5) |
| `section` | the section (`area/<area>_X`) whose cells and portals are closest to the box (goes to 0xB8) |

The pending scene is the one whose trigger box is closest to the checkpoint's box; on a near tie (within 25%) the
later trigger wins, and checkpoints of a later table chapter never get an earlier scene than the chapter before.
The checkpoint numbers themselves are **not** story order (cp02a_05 is before cp02a_02); the table's chapter
column is.

**Checked against real saves** (sandbox copies, Foster's saves untouched): rebuilding cp02a_05 from a cp02a_02 save
gives exactly the real cp02a_05 save's flags (25), numbers (all 0), area, section `hon_under_B` and pack
`block_B_s02a02_100_p`; Derrick lands 16 units from where the real save had him. For cp02a_02 the scene, pack and
section match; the flags differ by 3 (270 missing: set when the burger scene starts; 354/356 extra).

**Less sure:** the mid-area checkpoints of bettou (several land on the last scene, 3tbt), and every "area" method
checkpoint (hon_hon, plant, labo1, labo2, park..ending): their areas' own scene flags are left off, so the chapter
script there restarts the area's story from its first scene while Derrick stands at the checkpoint. Area starts are
the safest jumps there.

## 5. What `save_checkpoint.py` writes now

Index 0x08, area 0x90, section 0xB8, pack 0xE0, name 0x120, vsn 0x140 (as before), plus:
- numbers 0x59C4 = 0, flags 0x6964 = the table's flags (flag 2007 kept from the input save);
- event-state counts 0x0C / 0x0E = 0 and saved-object counts 0x6A98 / 0x6AA8 / 0x6AB8 = 0, so the level uses its
  own defaults (the input save's event ids belong to its own area; the chapter script's `Setting` rebuilds doors and
  items from the flags);
- Derrick and Alex at the checkpoint's trigger box;
- Derrick's AI script id (+0x2E of his block) = the area's chapter control script (`hero_ai` in the table; for
  labo1, labo2, opening and ending, which have none, the area's earliest own scene script); the MI_EVENT list is
  emptied with the event lists.

**Why cross-area jumps stayed black (in-game test of the first version, 2026-11-14):** jumps inside hon_under
worked, jumps to bettou loaded the area and then never updated Derrick, with or without the new flags and event
lists. The input save (cp02a_02) was made while Derrick ran the burger-room scene script (id 989), which only
hon_under's pack carries. In bettou that script does not exist, so he is never rebuilt. A real cp02a_05 save holds
810 (the chapter script, in both areas' packs); the rebuilt cp02a_05 now matches it byte for byte in that block.
`--keep-ai` keeps the input's id (for bisecting).

**Slot folders:** each slot needs its own `SaveMeta.xbx` with `Name=GAME nn` next to `data.sav`, or the load
menu shows EMPTY. Copy the input slot's folder, replace `data.sav`, set the Name line.
`--keep-flags`, `--keep-events`, `--keep-pos` and `--block` turn each part off.

## 6. Open

- Replay the other chapter scripts (s03 `EV_Pack_check`, s04, s07, s08, s09) so their areas get scene-exact flags.
- labo1 / labo2 have no chapter script: find what drives their story (area scripts or events).
- Decode the 0x84-byte records (0x4944) and the saved-object blocks; check whether weapons / powers need setting
  for far jumps.
- Confirm in game (test plan in the PR and in the kit's P4.7 notes).
