# Characters: tasks, status blocks, life, roster and outfits

Decomp roadmap Phase 5, task P5.3. Read from the game's own code (Ghidra decompile of `default.xbe`, the
recompiled C in `src/recomp/gen/`, and data tables in the program file). The game was not run for this.
Names live in `config/function_names.csv` ("sure" = code read end to end, "likely" = one strong clue).
Addresses are game (Xbox) addresses.

## The short version

- Every person in the game (Derrick, Alex, T'lan, soldiers, researchers...) is two things:
  1. a **character task** (an event object, kind `+0x3C == 1000`) that lives in the task tree, and
  2. a **status block** of 0x79A0 bytes in the "man" pool at `0x2A97A0`. The task keeps the slot number at
     `task+0x142`; `Char_GetStatusBlock` (0x4FF20) turns it into a pointer.
- The hero is not special in the code. He is the character task created **without flag 2** (`task+0x154`),
  so `Main_RegisterObj` (0x3A160) stores him in `[0x2A92D0]` and `CharTask_Create` names him `HERO`.
  Enemies and NPCs use the same task, the same update and the same status block.
- **Life, stamina, level, team and invincibility** sit together in a 0x34-byte **life block** at
  `status+0x5AAC`. Most life code takes a pointer to that block, not to the status block.
- The **roster** at `0x248FE0` lists 68 characters (+6 shadow entries). Each entry has up to 3 **outfits**;
  the status block keeps a pointer to its entry (`+0x6018`) and the index of the outfit shown (`+0x601E`).

## 1. Life of a character

```
level file CHAR_CREATE (0x413C0) / hero start (CharTask_CreateHero 0x3F2B0)
  -> CharTask_Create 0x3EDE0     task with update CharTask_Update; name HERO / spawn name / UnknownMan
       skips the spawn when its saved record says dead (CharRecord_IsGone 0x3A6C0)
  -> CharTask_Setup 0x3DCF0      task flags +0x154; slots +0x140/+0x142/+0x144 = -1
     or CharTask_Activate 0x3E470 (wakes a dormant spawn: AI brain +0x144, status block)
       -> Char_Alloc 0x4FE90     free "man" slot -> Char_Init 0x4DB20 -> status id
  every frame: CharTask_Update 0x3E710 -> Char_Update 0x4E350
               CharTask_Draw   0x3AD30 -> Char_Draw   0x4FA80
  end:         Char_Release 0x4FEF0 -> Char_Cleanup 0x4E250 -> slot freed
```

The pools are made once by `CharPool_Init` (0x503F0) and freed by `CharPool_Free` (0x504B0):

| Pool | Address | Name in the code | Item size | Count | Used for |
|---|---|---|---|---|---|
| man | 0x2A97A0 | `man` | 0x79A0 | given by caller (15 seen in hon_under) | status blocks |
| p_script | 0x2A97E8 | `p_script` | 0xA0 | 4 | extra script state, status `+0x5B88` (classes 1, 0xE-0x10) |
| cloth | 0x2A97D0 | `cloth` | 0xA10 | 2 | cloth simulation, status `+0x7940` (Stephania, Dr.) |
| hair | 0x2A97B8 | `hair` | 0x7A0 | 4 | hair, status `+0x7942` (Alex, Burns, fake Alex, Stephania) |

Pools are the vslib "TSS_MEM_CTRLR" kind: header `+2` used count, `+4` capacity, `+8` item size,
`+0x10` used flags, `+0x14` items. `Pool_Get` (0x493F0) = items + n * size.

### Character task fields (event object)

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x3C | s16 | object kind, 1000 = character | every `Obj_*` getter checks it (`Obj_IsChar` 0x3AE10) |
| +0x140 | s16 | slot made by 0x54070 (pad / input slot for the hero) | CharTask_Activate |
| +0x142 | s16 | **status id** (man pool slot) | `Char_GetStatusBlock`, all getters |
| +0x144 | s16 | AI brain id (0x53780) | CharTask_Activate, CharRecord_Write |
| +0x146 | s16 | riding / attached to something (draws through 0x561D0 instead) | CharTask_Draw |
| +0x148 | s16 | saved-record slot in pool 0x2A9310 | CharRecord_Alloc, Obj_GetCharRecord |
| +0x154 | u32 | flags: bit 1 = not the hero (NPC), 0x4000 = off, 0x8000 = active, 0x80000 = has record | Main_RegisterObj, Obj_GetCharFlags |
| +0x15A | s16 | role slot (1, 6, 0xE, 0xF, 0x10 -> main objects 0x2A92D4..0x2A92E4) | Main_RegisterObj |
| +0x15E | s16 | controller index | `Obj_GetPad` 0x3AE00, rumble in Life_DamageRumble |
| +0x1AC/+0x1B0 | | event file / event record from the level file | kit game-data-map (spawn test) |

## 2. Status block (0x79A0 bytes, man pool)

`Char_Init` (0x4DB20) writes almost every field below, so its order is the best map. "R/W" columns name
functions that were read; the counts come from a scan of the recompiled C for each offset.

| Offset | Type | Meaning | Written by | Read by |
|---|---|---|---|---|
| +0x000/+0x004/+0x008 | float x3 | **position** x, y, z | movement in Char_Update | CharRecord_Write, recomp FastMove / pos log |
| +0x010 | | facing data (Char_SetRotation builds matrices at +0x30/+0x70/+0xB0) | Char_SetRotation 0x49BB0 | 0x4A520 |
| +0x058..+0x06C | float x6 | velocity now / before | Char_Init | |
| +0x0F0..+0x128 | float | motion step this frame / last frame (copied by 0x49A30) | Char_Update helpers | |
| +0x130/+0x140 | | rotation quaternion -> matrix | Char_Init (0x1728C0, 0x105430) | |
| +0x168 | ptr | owning task (event object) | Char_Init | Char_Cleanup, LifeBlock_Init |
| +0x16C | s16 | motion state, 1 = normal play | Char_Init, Char_ResetMotion | 0x497D0 |
| +0x170 | u32 | copy of task flags `+0x154` (bit 1 = NPC, bit 2 = protected) | Char_Init | Char_IsProtected, 0x3A410 |
| +0x174 | s32 | extra id given to Char_Alloc (-1 = none) | Char_Alloc | |
| +0x178 | struct | motion-script runner (0x5EBD0 / 0x5EC40) | Char_Init | Char_Cleanup |
| +0x2DC | struct | movement controller (0x6DEE0) | Char_Init | Char_ResetMotion |
| +0x3A0 | struct | animation / motion set (0x5C440) | Char_Init | Char_ResetMotion |
| +0x4D0 | struct | skeleton and model parts for the roster entry (0x595D0) | Char_Init | |
| +0x12B0/+0x12B4 | ptr | bone matrix arrays (0x40 per bone) | | 0x4ABB0..0x4AD20, Char_HairUpdate |
| +0x49B4..+0x49BC | | per-class floats (class 0 / 3 / 4 values) | Char_Init | |
| +0x4A10 | struct | **attack list** (hit windows) | AttackList_Add 0x6F420 | Char_UpdateAttacks 0x5B020 |
| +0x4D80 | struct | **received-hit list** (16 entries) | 0x50650 init | HitList_Attacker 0x509F0 |
| +0x4F54 | struct | target / look-at helper (0x47D10) | Char_Init | 0x47E70, 0x48440 |
| +0x4F98, +0x508C, +0x5150, +0x54F0, +0x5A58 | struct | more sub-systems set up by Char_Init (not traced) | Char_Init | |
| +0x5550 | struct | **inventory** (item 0xE = clothes item used by Outfit_PickHero) | 0x51A60 init | 0x519C0, 0x4A8C0 |
| **+0x5AAC** | struct 0x34 | **life block** (section 3) | LifeBlock_Init 0x490D0 | many |
| +0x5AE0 | struct | 0x75170 helper; +0x5AE4 read by Char_Update | Char_Init | Char_Update |
| +0x5AE8 | float | value set by game command #284 (0x97DB0), clamped at 0 | 0x97DB0 | |
| +0x5AEC..+0x5B28 | | a small state machine (0x4D5A0..0x4DB00 set it up in different modes) | 0x4D610, 0x4D730... | Char_Update |
| +0x5B2C/+0x5B2E | s16 | push / step flags | 0x5A710, 0x49A30 | 0x49D90 |
| +0x5B30 | struct | 0x739A0 helper | Char_Init | Char_Cleanup |
| +0x5B88 | s32 | p_script pool slot (-1 = none) | Char_Init | 0x4BBC0..0x4BDB0, Char_Cleanup |
| +0x5C40..+0x5C48 | | flags, scale 1.0 | Char_Init | 0x497D0, 0x49D90 |
| +0x5C4C, +0x5C7C, +0x5CAC, +0x5D14 | struct | timers / attached-object holders (0x6E3B0) | Char_Init | 0x4A2F0, Char_ClothUpdate |
| +0x5DB4 | s32 x 84 | **character variables** | Char_SetVar 0x4A8A0 | Char_GetVar 0x4A880 |
| +0x5F04 | 15 x 12 bytes | slot table (cleared by Char_Init) | Char_Init | |
| +0x5FB8..+0x5FD8 | | ids (-1) and blend factors (0.5, 1.0) | Char_Init | 0x497D0 |
| +0x5FDC/+0x5FE0 | s32 | 4 / 4 | Char_Init | Char_ResetMotion |
| +0x5FE4 | s16 | mode 0/1/2 (game commands at 0x9DA90..0x9DAB0) | 0x4FA40..0x4FA60 | |
| +0x5FE6/+0x5FE8 | s16 + s32 | on/off (-1 = disabled) and a value (commands at 0x98A70..0x98AE0) | 0x4F960..0x4F9B0 | |
| +0x5FEC/+0x5FF4/+0x5FF8 | | an object link (pointer + id check) | 0x4F9F0, 0x4FA30 | |
| +0x5FFC/+0x6000 | ptr | **motion script** now / at spawn (`bhb_mo*` / `mo_*`, else script 0xB) | Char_Init, Char_SetMotScript | |
| +0x6004/+0x6008 | | motion-script argument now / before | Char_SetMotScriptArg 0x50360 | |
| +0x600C/+0x6010/+0x6014 | | queued motion script (flag, script, argument) | Char_QueueMotScript 0x502F0 | |
| **+0x6018** | ptr | **roster entry** | Char_Init (Roster_Find) | outfit code, Char_Draw, Roster_GetBindPose |
| +0x601C | s16 | outfit already picked | Char_ApplySpawnLife | Outfit_Pick, Outfit_OnItemSelect |
| **+0x601E** | s16 | **outfit index** shown | Outfit_Pick 0x4CBB0, Outfit_Set 0x4CC30 | Char_Draw, Char_PoseParts/DrawParts, Outfit_GetTag |
| +0x6020/+0x6024 | s16 | outfit variant state | Char_Init | Char_Draw |
| +0x6022 | u16 | variant word of outfit 0 (bit 3 added for the hero) | Char_Init | |
| +0x6026 | s16 | value given to Char_Init | Char_Init | |
| +0x6030 | struct | per-class shading/fade data (0x63D30) | Char_Init | Char_HairDraw, Char_ClothDraw |
| +0x6048 | | light value passed to hair/cloth draw | | Char_HairDraw, Char_ClothDraw |
| +0x6240, +0x6880, +0x6DC0, +0x7480 | 0x40 each | matrix arrays (25, 29, 19+19 identity at init) | Char_Init | |
| +0x7940/+0x7942 | s16 | cloth / hair pool slots | Char_ClothInit / Char_HairInit | their update/draw |
| +0x7944/+0x7948 | | cleared by Char_Init | | |
| +0x7950 | struct | 0x5E160 helper | Char_Init | |
| +0x799C | u16 | 1 / 2 flags from the task's controller (0x20B60 / 0x20CE0), 2 for NPCs | Char_Init | |

### Getters on a character object

All take the task, check `+0x3C == 1000`, then read the status block:
`Obj_GetLife` 0x3C070 (+0x5AB0), `Obj_GetLifeMax` 0x3C0B0 (+0x5AB4), `Obj_GetStamina` 0x3C0F0 (+0x5AB8),
`Obj_GetStaminaMax` 0x3C130 (+0x5ABC), `Obj_GetLifeLevel` 0x3C170 (+0x5ACE), `Obj_IsDead` 0x3C300
(+0x5AC8 bit 0), `Obj_GetTeam` 0x3BE40 (+0x5AD0), `Obj_GetLiveChar` 0x3C210 (block only if alive),
`Obj_SetStamina` 0x3C1A0, `Obj_GetChar` 0x3AE70 (block or 0). The HUD (0x12B230, 0x12B5A0) and the skills
panel (0x11E950) use these.

## 3. Life block (status + 0x5AAC, 0x34 bytes)

Set up by `LifeBlock_Init` (0x490D0) from Char_Init. Status offsets in brackets.

| Block | Status | Type | Meaning | Written by | Read by |
|---|---|---|---|---|---|
| +0x00 | +0x5AAC | s16 | **roster id** (10 = Derrick first person) | LifeBlock_Init | script getters 0x976C0, 0x7C110 |
| +0x02 | +0x5AAE | s16 | **class** = `Char_ClassOfType(roster id)` (table below) | LifeBlock_Init | 64 functions (outfit, hair, cloth, draw) |
| +0x04 | +0x5AB0 | float | **life** | Life_Add 0x48D50, Life_AddWithSource 0x48DA0, Life_Set 0x48F60, Life_Fill 0x48AB0 | Obj_GetLife, CharTask_Update -> HUD gauge |
| +0x08 | +0x5AB4 | float | **max life** | Life_InitMax 0x48B40, Char_ApplySpawnLife | Life_Ratio 0x49090 |
| +0x0C | +0x5AB8 | float | **stamina** | Stamina_Add 0x48FE0, Stamina_Set 0x48FA0, Stamina_Fill 0x48AC0 | Stamina_Ratio 0x49060 |
| +0x10 | +0x5ABC | float | **max stamina** | Life_InitMax | |
| +0x14 | +0x5AC0 | float | value set by game command #50 (0x97880) | 0x97880 | Char_Update |
| +0x18 | +0x5AC4 | float | value set by game command #51 (0x97890) | 0x97890 | Char_Update |
| +0x1C | +0x5AC8 | u16 | **state flags**: bit 0 dead, bit 1 set with bit 0 when LifeBlock_MarkDead gets 1, 0x04/0x08 (targeting skips these), 0x20 level changed, 0x80 life empty | LifeBlock_MarkDead 0x48F20, LifeBlock_UpdateEmptyFlag 0x48930 | Obj_IsDead, targeting 0x47AC0 |
| +0x1E | +0x5ACA | u16 | flags that changed this frame (cleared each frame) | same | Char_Update |
| +0x20 | +0x5ACC | s16 | **condition**: 0 fine, 1 hurt, 2 low, 3 dead (from life / max) | LifeBlock_UpdateCondition 0x49140 | 0x73CE0 |
| +0x22 | +0x5ACE | s16 | **life level** (power-up level; Derrick's max life 170 / 250 / 400 / 600) | Life_SetLevel 0x48E10, LifeLevel_Add 0x48E50 | Life_InitMax, Obj_GetLifeLevel |
| +0x24 | +0x5AD0 | s16 | **team** (default from class; 0..63) | LifeBlock_DefaultTeam 0x488C0, LifeBlock_SetTeam 0x49030 | Team_IsAllied, targeting (43 functions) |
| +0x26 | +0x5AD2 | s16 | 100 for NPCs, 0 for the hero; set by game command #427 (0x49050) | LifeBlock_Init | 0x79550, 0x7C230 |
| +0x28 | +0x5AD4 | s16 | cleared at init | | |
| +0x2A | +0x5AD6 | byte | **invincibility**: bit 0 on, bit 1 tied to one object; bits 2/3 second mode | Invuln_* 0x48A00..0x48AA0 | Char_IsProtected 0x50390, Hero_DamageMode 0x7D410 |
| +0x2C/+0x30 | +0x5AD8/+0x5ADC | ptr + id | the object the invincibility is tied to | Invuln_SetObj 0x48A40 | 0x5B0D0 (hit code) |

Notes:
- Life goes down through `Life_Add`/`Life_AddWithSource` (story scripts) and through the combat code
  (`Attack_ApplyHit` -> hit records; the recomp damage log scans the pool to see every change).
  `Life_AddWithSource` also calls `Life_DamageRumble` (0x48CE0), which starts rumble pattern 10/11/12 on the
  character's controller depending on the size of the hit.
- `Char_IsProtected` (0x50390): bit 0 or bit 2 of +0x2A, or status `+0x170` bit 2. The life script commands
  skip protected characters.
- `LifeLevel_Stash` / `LifeLevel_Unstash` (0x48E80 / 0x48EE0, game commands #1468 / #1469) keep the hero's
  level in `0x2A9798` and zero it, then put it back (where the story uses it is not checked).
- Saved copy: `LifeSave_Write` (0x49630) copies flags, class, level, life, max, stamina and max stamina into
  the character record (pool 0x2A9310, magic 0x1122 / 0x7532, `CharRecord_Write` 0x3A5C0). When the area is
  entered again, `CharRecord_IsGone` -> `LifeSave_IsDead` stops dead characters from coming back, except the
  main cast (`Class_IsMainCast` 0x48CA0: classes 0, 1, 0xE, 0xF, 0x10).

### Classes and teams

`Char_ClassOfType` (0x487A0) maps the roster id to a class; `LifeBlock_DefaultTeam` (0x488C0) maps the class
to a team. `Team_IsAllied` (0x48980) reads a 6x6 table at 0x244AC8: teams 0 and 3 are on the same side,
and teams 2 and 5; every team is allied with itself.

| Class | Who | Team |
|---|---|---|
| 0 | Derrick (and anyone not listed) | 0 |
| 1 | Alex (Navi) | 0 |
| 2 | T'lan soldier | 2 |
| 3 | heavy T'lan | 2 |
| 4 | female (stealth) T'lan | 2 |
| 5 | researchers, Brad, Ned, "Derrick as researcher", hallucination Derrick (85) | 3 |
| 6 | Burns | 5 |
| 7 | US soldiers (Gary, Ford, N white/black) | 1 |
| 8 | armed / half-armed T'lan | 2 |
| 9 | laser T'lan | 2 |
| 10 (0xA) | special forces | 1 |
| 11 (0xB) | Cyrus soldiers, future soldiers | 3 |
| 12 (0xC) | fake Alex | 2 |
| 13 (0xD) | women researchers, children | 3 |
| 14 (0xE) | Gianni | 3 |
| 15 (0xF) | Stephania (cloth + hair) | 3 |
| 16 (0x10) | Dr. (cloth) | 3 |

## 4. Roster and outfits

Roster table at **0x248FE0**: 68 entries of 0x34 bytes (the code loops 0x44), then 6 shadow entries.
Entry: `+0x00` s16 roster id, `+0x04` name (`lechara_*` with a Japanese note), `+0x08` float scale,
`+0x0C` 1 for women / girls, `+0x10` outfit count (1-3), `+0x14..+0x1C` outfit sets.
Outfit set: `+0` s16 tag, `+2` s16 variant count, then 0x1C-byte variant records (part lists at +8..+0x14;
see `src/recomp_manual.c` hero outfit code and the kit's `reports/research/skins-notes.md`).

| Function | What it does |
|---|---|
| `Roster_Find` 0x64380 | roster id -> entry (0 if none) |
| `Roster_GetBindPose` 0x64340 | roster id -> runtime slot `0x2A9F70 + n * 0x3250` (bind pose) |
| `Roster_BuildBindPoses` 0x64B60 / `Roster_BuildBindPose` 0x646F0 | build those slots at load (from 0x3F350) |
| `Roster_FpToTp` 0x64BA0 | Derrick first person (10) -> third person (9) when drawn from outside |
| `Roster_GetShadowDef` 0x64C10 | entry used for the shadow: Derrick's own; heavy / laser / stealth T'lan and children have their own; else shadow man or woman by the `+0x0C` flag |
| `Outfit_Pick` 0x4CBB0 | on spawn: class 0 -> `Outfit_PickHero` 0x4CAB0, class 1 -> `Outfit_PickAlex` 0x4CB20, else outfit 0 |
| `Outfit_PickHero` 0x4CAB0 | area byte 0x29C5F5 = 0 or 1 -> T shirt (tag 4); -2 -> keep; holding item 0xE -> normal |
| `Outfit_PickAlex` 0x4CB20 | area byte 0xE, 0x10-0x13, 0x16 -> ragged (tag 6); 0x14, 0x15 -> future (tag 5) |
| `Outfit_FindByTag` 0x64310 | tag -> outfit index |
| `Outfit_Set` 0x4CC30 / `Outfit_GetTag` 0x4CC60 | story scripts' set / get outfit |
| `Outfit_OnItemSelect` 0x4CC10 | selecting item 0xE clears "already picked" so the outfit is chosen again |
| `Char_PoseParts` 0x72650 / `Char_DrawParts` 0x72990 | pose and draw the parts of `sets[+0x601E]` |

The full list (index, address, id, name, class, team, female flag, outfit tags):

| # | Entry | Id | Name | Class | Team | F | Outfit tags |
|---|---|---|---|---|---|---|---|
| 0 | 0x248FE0 | 10 | lechara_me_fp_skin_shimono (hero, first person) | 0 | 0 | 0 | 0, 4, 3 |
| 1 | 0x249014 | 9 | lechara_me_tp_skin_shimono (hero, third person) | 0 | 0 | 0 | 0, 4, 3 |
| 2 | 0x249048 | 85 | lechara_derrick_skin_shimono (hero in the stage 5 hallucination) | 5 | 3 | 0 | 0, 4, 3 |
| 3 | 0x24907C | 8 | lechara_navi_skin_shimono (Alex) | 1 | 0 | 1 | 0, 6, 5 |
| 4 | 0x2490B0 | 78 | lechara_navi_nexus_skin_shimono (Alex) | 1 | 0 | 1 | 0, 6, 5 |
| 5 | 0x2490E4 | 64 | lechara_navi_boro_skin_shimono (ragged Alex) | 1 | 0 | 1 | 6, 5, 0 |
| 6 | 0x249118 | 65 | lechara_navi_FU_skin_shimono (future Alex) | 1 | 0 | 1 | 5, 0, 6 |
| 7 | 0x24914C | 28 | lechara_fake_navi_skin_shimono (fake Alex) | 12 | 2 | 1 | 0, 2 |
| 8 | 0x249180 | 1 | lechara_clone_A_skin_shimono (T'lan soldier) | 2 | 2 | 0 | 0 |
| 9 | 0x2491B4 | 14 | lechara_banz_skin_shimono (Burns) | 6 | 5 | 0 | 0 |
| 10 | 0x2491E8 | 80 | lechara_banz_yukai_skin_shimono (kidnapper Burns) | 6 | 5 | 0 | 0 |
| 11 | 0x24921C | 12 | lechara_clone_W_skin_shimono (female T'lan) | 4 | 2 | 1 | 0 |
| 12 | 0x249250 | 19 | lechara_clone_huge_skin_shimono (heavy T'lan) | 3 | 2 | 0 | 0 |
| 13 | 0x249284 | 20 | lechara_clone_armed_skin_shimono (armed T'lan) | 8 | 2 | 0 | 0 |
| 14 | 0x2492B8 | 79 | lechara_clone_halfarmed_skin_shimono (half-armed T'lan) | 8 | 2 | 0 | 0 |
| 15 | 0x2492EC | 21 | lechara_clone_laser_skin_shimono (laser T'lan) | 9 | 2 | 0 | 0 |
| 16 | 0x249320 | 62 | lechara_navy_gary_IK (US soldier Gary) | 7 | 1 | 0 | 0 |
| 17 | 0x249354 | 86 | lechara_navy_s2_gary_IK (stage 2 Gary) | 7 | 1 | 0 | 0 |
| 18 | 0x249388 | 70 | lechara_navy_ford_IK (US soldier Ford) | 7 | 1 | 0 | 0 |
| 19 | 0x2493BC | 87 | lechara_navy_s2_ford_IK (stage 2 Ford) | 7 | 1 | 0 | 0 |
| 20 | 0x2493F0 | 7 | lechara_navy_N_W_morioka (white N soldier) | 7 | 1 | 0 | 0 |
| 21 | 0x249424 | 89 | lechara_navy_s2_N_W_IK (stage 2 white N soldier) | 7 | 1 | 0 | 0 |
| 22 | 0x249458 | 2 | lechara_navy_N_B_morioka (black N soldier) | 7 | 1 | 0 | 0 |
| 23 | 0x24948C | 33 | lechara_navy_S_IK (special forces) | 10 | 1 | 0 | 0 |
| 24 | 0x2494C0 | 40 | lechara_navy_S_Mhand_IK (special forces, hand animation) | 10 | 1 | 0 | 0 |
| 25 | 0x2494F4 | 68 | lechara_navy_marlon_IK (Cyrus soldier Marlon) | 11 | 3 | 0 | 0 |
| 26 | 0x249528 | 69 | lechara_navy_smith_IK (Cyrus soldier Smith) | 11 | 3 | 0 | 0 |
| 27 | 0x24955C | 36 | lechara_navy_C_W_IK (Cyrus white N soldier) | 11 | 3 | 0 | 0 |
| 28 | 0x249590 | 35 | lechara_navy_C_B_IK (Cyrus black N soldier) | 11 | 3 | 0 | 0 |
| 29 | 0x2495C4 | 48 | lechara_navy_FU_W_IK (future white soldier) | 11 | 3 | 0 | 0 |
| 30 | 0x2495F8 | 49 | lechara_navy_FU_B_IK (future black soldier) | 11 | 3 | 0 | 0 |
| 31 | 0x24962C | 39 | lechara_brad_skin_shimono (Brad) | 5 | 3 | 0 | 0 |
| 32 | 0x249660 | 17 | lechara_res_sambo_IK (Sambo) | 5 | 3 | 0 | 0 |
| 33 | 0x249694 | 55 | lechara_res_ian_IK (researcher Ian) | 5 | 3 | 0 | 0 |
| 34 | 0x2496C8 | 88 | lechara_res_s2_ian_IK (stage 2 Ian) | 5 | 3 | 0 | 0 |
| 35 | 0x2496FC | 56 | lechara_res_james_IK (researcher James) | 5 | 3 | 0 | 0 |
| 36 | 0x249730 | 57 | lechara_res_toad_IK (researcher Toad) | 5 | 3 | 0 | 0 |
| 37 | 0x249764 | 46 | lechara_FP_skin_IK (hallucination hero as researcher, first person) | 5 | 3 | 0 | 0 |
| 38 | 0x249798 | 47 | lechara_TP_skin_IK (same, third person) | 5 | 3 | 0 | 0 |
| 39 | 0x2497CC | 58 | lechara_res_ray_IK (researcher Ray) | 5 | 3 | 0 | 0 |
| 40 | 0x249800 | 59 | lechara_res_berry_IK (researcher Berry) | 13 | 3 | 1 | 0 |
| 41 | 0x249834 | 73 | lechara_res_s05_A_IK (researcher A, hand animation) | 5 | 3 | 0 | 0 |
| 42 | 0x249868 | 74 | lechara_res_s05_B_IK (researcher B) | 5 | 3 | 0 | 0 |
| 43 | 0x24989C | 75 | lechara_res_s05_C_IK (researcher C) | 5 | 3 | 0 | 0 |
| 44 | 0x2498D0 | 76 | lechara_res_s05_D_IK (researcher D, woman) | 13 | 3 | 1 | 0 |
| 45 | 0x249904 | 4 | lechara_res_A_W_morioka (white researcher) | 5 | 3 | 0 | 0, 1 |
| 46 | 0x249938 | 16 | lechara_charred_res_A_IK (burnt researcher) | 5 | 3 | 0 | 1 |
| 47 | 0x24996C | 5 | lechara_res_A_B_morioka (black researcher) | 5 | 3 | 0 | 0, 1 |
| 48 | 0x2499A0 | 6 | lechara_res_A_Y_morioka (yellow researcher) | 5 | 3 | 0 | 0, 1 |
| 49 | 0x2499D4 | 31 | lechara_res_F_W_IK (white woman researcher) | 13 | 3 | 1 | 0 |
| 50 | 0x249A08 | 32 | lechara_res_F_B_IK (black woman researcher) | 13 | 3 | 1 | 0 |
| 51 | 0x249A3C | 30 | lechara_res_F_Y_IK (yellow woman researcher) | 13 | 3 | 1 | 0 |
| 52 | 0x249A70 | 53 | lechara_child_alex_IK (child Alex) | 13 | 3 | 1 | 0 |
| 53 | 0x249AA4 | 54 | lechara_child_ernest_IK (child Ernest) | 13 | 3 | 0 | 0 |
| 54 | 0x249AD8 | 41 | lechara_child_F_W_IK (child, girl) | 13 | 3 | 1 | 0 |
| 55 | 0x249B0C | 50 | lechara_child_F_Y_IK (child, girl) | 13 | 3 | 1 | 0 |
| 56 | 0x249B40 | 51 | lechara_child_W_IK (child, boy) | 13 | 3 | 0 | 0 |
| 57 | 0x249B74 | 52 | lechara_child_B_IK (child, boy) | 13 | 3 | 0 | 0 |
| 58 | 0x249BA8 | 13 | lechara_dr_skin_shimono (Dr.) | 16 | 3 | 0 | 0 |
| 59 | 0x249BDC | 66 | lechara_dr_FU_skin_shimono (future Dr.) | 16 | 3 | 0 | 5 |
| 60 | 0x249C10 | 38 | lechara_stephania_skin_shimono (Stephania / Rita) | 15 | 3 | 1 | 0, 7 |
| 61 | 0x249C44 | 37 | lechara_gianni_skin_shimono (Gianni / Cyrus) | 14 | 3 | 0 | 0, 6 |
| 62 | 0x249C78 | 81 | lechara_gianni_dasyutu_skin_shimono (escaping Gianni) | 14 | 3 | 0 | 0 |
| 63 | 0x249CAC | 72 | lechara_gianni_boro_skin_shimono (dying Gianni) | 14 | 3 | 0 | 6, 0 |
| 64 | 0x249CE0 | 45 | lechara_ned_skin_shimono (future researcher Ned) | 5 | 3 | 0 | 0 |
| 65 | 0x249D14 | 42 | lechara_FU_W_IK (future white researcher) | 5 | 3 | 0 | 0 |
| 66 | 0x249D48 | 43 | lechara_FU_B_IK (future black researcher) | 5 | 3 | 0 | 0 |
| 67 | 0x249D7C | 44 | lechara_FU_Y_IK (future yellow researcher) | 5 | 3 | 0 | 0 |
| 68 | 0x249DB0 | - | shadow man | | | 0 | 0 |
| 69 | 0x249DE4 | - | shadow woman | | | 1 | 0 |
| 70 | 0x249E18 | - | shadow heavy T'lan | | | 0 | 0 |
| 71 | 0x249E4C | - | shadow laser T'lan | | | 0 | 0 |
| 72 | 0x249E80 | - | shadow stealth T'lan | | | 1 | 0 |
| 73 | 0x249EB4 | - | shadow child | | | 0 | 0 |

Outfit tags seen: 0 normal, 1 burnt, 2 fake Alex as a female T'lan, 3 researcher disguise, 4 T shirt,
5 future, 6 ragged / dying, 7 Stephania's second outfit (not looked at).

## 5. Hero vs enemies and NPCs

- Same task (`CharTask_Update`), same status block, same life code. The differences are data:
  task flag 2 (NPC), class (0 = Derrick), team, and which AI brain / motion script runs.
- The hero: `[0x2A92D0]` (`Hero_GetObj` 0x3A2C0), set by `Main_RegisterObj` when a character without flag 2
  registers. Other main objects by role: `[0x2A92D4]` (role 1), `[0x2A92D8]` (6), `[0x2A92DC]` (0xF),
  `[0x2A92E0]` (0x10), `[0x2A92E4]` (0xE).
- Max life comes from `Life_InitMax` (0x48B40): Derrick by life level (170 / 250 / 400 / 600), others by
  class, times `Life_DifficultyScale`. `Char_ApplySpawnLife` then applies the spawn record's saved life.
- `CharTask_Update` sends the hero's life and max life to HUD gauge 0 (`HudGauge_SetValue` 0x12A3C0,
  `HudGauge_SetStart` 0x12A3E0).
- Debug "HERO damage Normal / Invincible" (game command #728, 0x7D410) reads the invincibility bits of the
  life block; the PC NoDamage switch (`src/recomp_manual.c`) keeps life at max instead.

## 6. Renamed here

- 0x3E710 `Hero_Update` -> **`CharTask_Update`**: `CharTask_Create` installs it for every character (it
  names the task `HERO` or `UnknownMan` but uses the same update). The recomp hooks on it still work: they
  check `task == [0x2A92D0]` before doing hero things.
- 0x4FF20 `Char_GetStatusBlock` raised from likely to sure (it is `Pool_Get` on the man pool).

## 7. Left open

- `Char_Update` (0x4E350, 3,000 lines of recompiled C) is only mapped by the helpers it calls; its steps are
  not split out yet.
- The small state machine at status +0x5AEC..+0x5B28 (0x4D5A0..0x4DB00) and the fields +0x5FE4..+0x5FF8 are
  set by game commands whose meaning the script-command work (P4.6f) may settle.
- Life block +0x26 (100 for NPCs) and +0x5AC0 / +0x5AC4 (game commands #50 / #51): written and read, meaning
  not proven.
- Bits 1, 2 and 3 of the state flags (+0x5AC8) and the second invincibility mode (bits 2/3 of +0x2A):
  which one means "can't die" vs "takes no hits" needs a live test.
- Sub-structs at +0x4F98, +0x508C, +0x5150, +0x54F0, +0x5A58, +0x5AE0, +0x5B30 are only known by their
  init functions.
- Game-command wrappers that read these fields (0x7C110..0x7D230, 0x97710..0x978B0) are left to the
  script-command names so the two lists do not clash.
