# Engine core: tasks, memory, paks and loading

Decomp roadmap Phase 5, task P5.2. Read from the game's own code (Ghidra decompile) and its debug strings.
Nothing here changes the generated code; the names live in `config/function_names.csv`.
Addresses are game (Xbox) addresses. "sure" / "likely" in the names file says how certain each name is.

The engine is a Japanese in-house library the code calls **vslib** (messages start with `vslib:`) plus a
game layer written by a programmer who signs debug lines `makino:`.

## The short version

- Everything that runs each frame is a **task**. The game calls them "heaps" (`vslib: Heap Num Limit Over!`).
  Tasks sit in a tree. Each has an update function, a free function and a few more callbacks.
- Memory is one big block cut into named pieces by **vsMemory** (`vsMemoryAlloc [name]`). Every piece has a
  name, so the debug memory map can say who owns what.
- Game data comes in **paks** (`BDPK` files). The **data manager** keeps 15 pak slots and a lookup table per
  data kind (models, textures, sounds...).
- An **area load** is a task that walks through steps: free the old level, pick disc or hard-disk cache, read
  the area's `.pak`/`.nut`/`.ev` files, register them. A side task copies areas to the hard disk cache.
- Reads go through a small **file system** with device prefixes (`sim:` = disc data, `cache:` = hard disk,
  memory units) and one IO thread that serves a queue of requests.

## 1. Task system ("heaps")

### Pools

`Memory_InitHeaps` (0x22650) makes two pools with `VsHeap_InitPool` (0x17DBF0):

| Pool | Address | Use |
|---|---|---|
| MainHeapBuffer | 0x298A40 | every game task (the `&DAT_00298a40` you see in 260+ callers) |
| VBlankHeapBuffer | 0x299280 | small pool (0x20 tasks of 0x100 bytes) for vblank work |

Pool header (0x840 bytes):

| Offset | Meaning |
|---|---|
| +0x00 | node array start |
| +0x04 | memory block (from vsMemory) |
| +0x08 | first task of the flat list (the whole tree in order) |
| +0x0C / +0x10 | free list head / tail (normal nodes) |
| +0x14 | max normal tasks |
| +0x18 | normal node size |
| +0x1C | tasks in use |
| +0x24 | EX node area start |
| +0x28 / +0x2C | EX free list head / tail |
| +0x30 / +0x34 / +0x38 | EX max / EX node size / EX in use |
| +0x3C | next id to try |
| +0x40 | id bitmap, 0x800 bytes (ids 0..0x3FFE) |

### Task node

| Offset | Size | Meaning |
|---|---|---|
| +0x00 | 4 | id (0x3FFF = free node) |
| +0x08 | 1 | state: 0 free, 1 always runs (even when paused), 2 runs only when not paused |
| +0x09 | 1 | 1 = event task (has an event id) |
| +0x0A | 1 | flags; bit 0 = has a draw function |
| +0x0B | 1 | flags; 0x80 = skip update, 0x40 = also update children here |
| +0x0C | 2 | number of children |
| +0x0E | 2 | node header size (0x38) |
| +0x10 | 4 | parent |
| +0x14 / +0x18 | 4 | previous / next in the flat list |
| +0x1C | 4 | name (`"DATA MANAGER"`, `"FADE_MANAGER"`, `"LOAD_HDD_CACHE"`...) |
| +0x20 | 4 | update function, called once per frame |
| +0x24 | 4 | free function, called when the task is killed |
| +0x28 | 4 | draw function |
| +0x2C | 4 | debug info function (prints the task's state on a debug page) |
| +0x31 | 1 | 1 = node came from the EX pool |
| +0x32 | 1 | draw layer / order |
| +0x34 | 4 | step number (most tasks are small state machines on this) |
| +0x38.. | | task's own data |

The tree is stored as one flat list in tree order: a task is followed by its children. `+0x0C` says how
many children follow, which is how the walk functions skip or visit sub-trees.

### Main functions

| Function | Address | What it does |
|---|---|---|
| VsHeap_New | 0x17DDF0 | (pool, parent, index) new task as a child of parent; -1 = last |
| VsHeap_NewEx | 0x17DF70 | same from the EX pool |
| VsHeap_Kill | 0x17E240 | (pool, task) kill one task; its children move up to its parent |
| VsHeap_KillTree | 0x17E280 | kill a task and everything under it |
| VsHeap_KillChildren | 0x17E2D0 | kill everything under a task, keep the task |
| VsHeap_UpdateAll | 0x17E350 | per frame (from `Game_Update`): call every task's update |
| VsHeap_ForEachChild / ...Until / ...Arg / ...Named | 0x17E450 / 0x17E4D0 / 0x17E560 / 0x17E730 | walk a sub-tree |
| VsHeap_FindChildByName | 0x17E600 | find a direct child by name |

Kill order: the free function (+0x24) runs first, then the node goes back on its free list and its id bit is
cleared. Free functions are where tasks give back their memory, so a missing kill = a memory leak.

### Pause

`Game_Update` (0x22410) calls `VsHeap_UpdateAll(pool, after_fn, paused)`. With `paused` set, only state-1
tasks run. The pause mode lives at 0x29F6E0: 0 running, 1 user pause (`USER PAUSE`), 2 system pause
(`SYSTEM PAUSE`, used by loaders), 3 resuming. A system pause holds while any reason bit in 0x29F6F0 is set
(`Game_SystemPauseFor` / `Game_SystemPauseRelease`); loaders use bit 4.

### Game layer on top: event tasks

The game wraps tasks as **event tasks** (`EvTask_Create` 0x31C10, `EvObj_Create` 0x31CA0): a task plus an
event id from a 0x400-entry table, so scripts can find tasks by id or name (`Event_FindById`,
`Event_FindByName`). The **event base** (pointer at 0x2A8DF8) holds 8 **levels**; each level record is 0x54
bytes at base+0x38+level*0x54:

| Offset | Meaning |
|---|---|
| +0x38 | loaded id (-1 = empty) |
| +0x3A | flags (bit 0 = registered) |
| +0x3C | file block (vsMemory node) |
| +0x40 | level root task (all of that level's tasks hang here) |
| +0x48 | owner value |

Level numbers match pak slot numbers: freeing level n (`EventLevel_Free`) and pak slot n (`Data_FreeSlot`)
go together. Level 3 is the area, 6 the event pak.

## 2. Memory manager (vsMemory)

Manager pointer at 0x430E84, set up by `VsMemory_Init` (0x17E7E0, prints `----InitMemoryManager----`).

| Offset | Meaning |
|---|---|
| +0x00 | number of map nodes |
| +0x04 | node array |
| +0x08 | first block (`MemoryMapList`, the node array itself) |
| +0x0C | free list head (sorted by size) |
| +0x10 | unused node list |
| +0x14 / +0x18 | memory top / bottom |
| +0x1C | next block id |
| +0x20 | free bytes |
| +0x24 / +0x28 / +0x2C | active / free / unused node counts |
| +0x30 | "memory over" callback (if set, called instead of hanging) |

Each block is a 0x24-byte **map node**:

| Offset | Meaning |
|---|---|
| +0x00 | data address (so code does `*block` to get the data) |
| +0x04 | size (always rounded up to 0x80) |
| +0x08 | id |
| +0x0C | type: 0 none, 1 free, 2 in use |
| +0x10 | name (`"DATA_TABLE"`, `"DATAPAK_LEV6"`, file names...) |
| +0x14 / +0x18 | previous / next block by address |
| +0x1C / +0x20 | previous / next on the free list |

| Function | Address | What it does |
|---|---|---|
| VsMemory_Alloc | 0x17EBF0 | (name, size) first free block that fits; exact fit reuses it, else splits |
| VsMemory_Free | 0x17EDB0 | gives a block back and merges with free neighbours |
| VsMemory_Resize | 0x17F0B0 | grows into the next free block or cuts off the tail |
| VsMemory_Compact | 0x17EFE0 | `GabageCollect %dbyte`: slides blocks after a marker down to close gaps |
| VsMemory_DumpMap | 0x17D9C0 | `[ MEMORY MAP ]` list of every block |

**MEMORY_OVER** (inside VsMemory_Alloc, about 0x17ED9E): when nothing fits, it prints
`vsMemoryAlloc [name] ERROR !! (size:..)`, then calls the "memory over" callback if one is set; if not it
prints `MEMORY_OVER`, dumps the memory map and loops forever. That endless loop is what a "memory full" hang
looks like.

The garbage-collect marker: `Memory_GarbageCollect` (0x21C10) allocates a tiny block named
`GABAGE COLLECTION TOP`; later `Game_Update` calls `VsMemory_Compact` from that block on.

## 3. Data manager (paks)

`Data_Init` (0x1AE70, from `Game_Init`) makes the `DATA MANAGER` task; its pointer is kept at 0x29F638.
The task's own data:

| Offset | Meaning |
|---|---|
| +0x3C + kind*0x10 | count A for this data kind |
| +0x40 + kind*0x10 | count B |
| +0x44 + kind*0x10 | id table size (0x2400 for kind 0, 0x1200 for kind 1, ...) |
| +0x48 + kind*0x10 | id table (vsMemory block `DATA_TABLE`): per id, 2 shorts = (slot, entry index), -1 = not loaded |
| +0xBC + slot*0xC | slot record (15 slots, 0..14), see below |

Slot record (12 bytes):

| Offset | Meaning |
|---|---|
| +0 (0xBC) | 1 = slot uses a fixed buffer (slot 6 gets `DATAPAK_LEV6`, about 2 MB; 3 MB `DATAPAK_LEV6_DB` on 128 MB dev kits) |
| +1 (0xBD) | state: 0 empty, 1 loaded, 2 reserved, `'x'` released (freed 2 frames later by `Data_Update`) |
| +2 (0xBE) | frame counter for the delayed free |
| +4 (0xC0) | pak block (vsMemory node) |
| +8 (0xC4) | owner value |

A pak starts with `BDPK`. From +0x40 come 0x80-byte entries starting with `BD`, then the kind (byte 2),
the id (short at +4), a "mapped" mark `'f'` (byte 7) and the name (+0x14). Kinds seen: 1 model, 2 texture,
4 sound bank, 5 handled by 0x115510.

Loading a pak:

1. `Data_LoadPak` (0x1B520, sync) or `Data_LoadPakAsync` (0x1A1D0) reads `sim:romdata/<name>`.
2. `Data_RegisterPak` (0x1B4C0) checks `BDPK` and stores the block in the slot.
3. `Data_MapIds` (0x1B3A0) fills the id tables (`makino: Duplicate Type:%d ID:%d` if an id is taken), then
   sets up each kind: `Data_SetupTextures` (0x1A870, may shrink the pak: `PakFileResize`),
   `Data_SetupModelBuffers` (0x1AA10), `Data_SetupSounds` (0x1B1F0, starts a `SOUND_SEND` task),
   `Data_SetupScripts` (0x1ACE0).

Lookups: `Data_FindEntry` (0x1A2D0) and `Data_GetPointer` (0x1A330) turn (kind, id) into the entry or its
data; `Data_GetTextureHandle` (0x1A550, 152 callers) and `Data_GetModelLod` (0x1A570) are the common ones.

Freeing: `Data_FreeSlot` (0x1B130) prints `makino: DATA PAK FREE %d`, frees textures, model buffers, sounds
and kind 5 data, clears the ids, and marks the slot `'x'`. The memory is given back two frames later.

## 4. File system and loading

### Paths and devices

`FS_Open` (0x19BDF0) → `FS_ResolvePath` (0x19D700) maps a prefix to a real drive:
`sim:` = disc data (`sim:romdata/...`), `cache:` = hard disk cache, plus memory units. Device table at
0x28E44C (13 devices). Up to 16 files open at once (handle table, `FS_InitHandleTable`).

### Sync and async reads

- Sync: `File_ReadWhole` (0x17F260) = open, `FS_GetSize`, `VsMemory_Alloc(name, size)`, `FS_Read`, close.
  `File_ReadRomdata` (0x17F690) is the same with `sim:romdata/` in front.
- Async: `File_LoadAsync` (0x17F3D0) has 8 load slots at 0x276FB8 (`vslib: Load Max Over` when full). It
  opens the file, allocates if no buffer was given, and queues a read with `FS_ReadAsync` (0x19C0A0).
- The **IO thread** (started by `FS_StartIoThread` 0x1A0410) takes requests from a priority list
  (`FSReq_Submit` 0x19F010). Request types: 2 read, 3 write, 6 copy, 8 remove folder, 0x20/0x21 sync/stop.
  Reads go in 0x4000-byte steps through a 0x2000-byte read cache. When a request is done its callback runs
  (`File_AsyncDoneCb` 0x17F320 closes the file and calls the loader's callback).

### Area load

`Area_Load` (0x2BFC0) looks up the area table at [0x2A8E38] (0x50-byte entries: name at +0, block letter at
+0x17, start position at +0x40..+0x4C) and prints `makino: area_load area/<name> / BLK<letter> CACHE:<n>`.
Special values: 0 = `area/opening`, 0x100 = `area/extra_back`. It calls `Area_LoadAsync` (0x2DF10) →
`AreaLoad_Start` (0x2DD50), which creates the task `EVENT_LOAD_ALL_HEAP(APP)` with the file names
`<name>.pak`, `<name>.nut`, `<name>.ev`, `<name>_out.ev`, and picks the source: `cache:` if
`AreaCache_Check` (0x2C9E0) finds `cache:area/<name>_chk`, else `sim:romdata/`.

`AreaLoad_Update` (0x2D450) then steps (+0x34):

1. Wait for the level to be free; mark it busy (bit in 0x2A8E58).
2. `makino:Event & Data Free level [n-5]`: `EventLevel_Free` + `Data_FreeSlot` for levels n..5.
3. `makino:Wait Data Free`: wait until they are gone, then `Texture GargageCollect` (`Tex_Compact`).
4. Load the files (async); on failure `AreaLoad_Retry` (`makino: Load Retry %d`) falls back to
   `sim:romdata/`.
5. Register: `Data_RegisterLoadedPak`, `EventLevel_SetFile`, `Event_RegisterLevelData`
   (`makino:EVD DATA REGIST`, `AREA DATA SET`).
6. Kill itself; `AreaLoad_Free` (0x2C500) runs the done callback and ends the system pause.

While this runs the game is in system pause (`Game_SystemPauseFor(4)`), so only state-1 tasks update.

### Event paks

`EventPak_LoadCheck` (0x2DB40, `EVENT PAK LOAD CHECK`) starts an `EvePakLoadWait` task;
`EventPak_LoadStart` (0x2D120) creates `EVENT_LOAD_PAK_HEAP(APP)`, whose update `EventPak_Update` (0x2CCF0)
frees level/slot 6 (`Event & PakData Free level`), waits, loads `<name>.pak` into the fixed buffer
(`evepak load start`, up to 20 retries) and finishes (`event pak %s Load Finish`).

### Hard disk cache

`HddCache_Start` (0x2D320) creates the `LOAD_HDD_CACHE` task (the opening area is cached as `HON_UNDER`,
7F/Main Research Center). `HddCache_Update` (0x2CEB0): skip if `cache:area/<name>_chk` exists
(`hddcache: LOAD CANCEL`), else clear `cache:area`, make the folder, copy
`sim:romdata/area/<name>` → `cache:area/<name>` (`FS_CopyAsync`), then write the `_chk` file.
It waits while an event pak load is running, so the copy never fights a level load for the disc.

## What this means for loading quality (LOADING_PLAN.md)

- Area loads are already async and split into steps; the long waits are the disc read and the texture
  compaction, both in `AreaLoad_Update`. Timing the step changes of that task (+0x34) is the cleanest way to
  measure load time per step.
- Only one file request runs at a time (one IO thread, one list). The HDD cache copy is held back while
  loads run.
- On PC the `cache:` check decides the path. If `cache:area/<name>_chk` exists the cache copy is used.
- A full memory pool hangs in MEMORY_OVER unless a "memory over" callback is set at manager+0x30; a
  recomp hook there could log and recover instead of freezing.

## Not done yet

- The IO thread's main loop (0x1A0300) and the request handlers for types 4, 5, 0xC-0xE are not named.
- Kind 5 pak data (0x115510 / 0x114BF0) is not identified.
- The meaning of count A / count B per data kind is a guess.
