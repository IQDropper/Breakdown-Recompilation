# Behaviour scripts: BHEV files and AAS2 bytecode (P4.6)

Breakdown runs every character's moves, every enemy brain and most story events as small compiled programs.
This page explains the file layout, the interpreter in `default.xbe`, every opcode, and how a script reads buttons,
plays animations, deals damage, branches and calls. Worked out statically (no game run) on 2026-11-14.

Tools: `tools/aas2_dis.py` (disassembler, move table) and `config/aas2_natives.csv` (the 1,839 game commands,
87 named so far). Function names: `config/function_names.csv` (section "AAS2 script interpreter").

**In one paragraph:** each script is a set of **states** (a start block, a run block that runs every frame, a stop
block) plus **subroutines** and **data tables**. The code is a small stack machine with 65 opcodes; the game's own
table even holds their names (`OP_PSHI`, `OP_CAL`, `OP_JMP`...). Almost all real work is done by **game commands**
(1,839 of them) that the script calls by number: "play this animation", "is this button pressed", "add a hit
window", "change life". All characters, Derrick and enemies alike, share one move script, `bhb_mo_hero`, which picks
values per **character type** from its data tables. Enemy brains (`bhb_ai_*`) decide what to do.

## 1. Where scripts live

Data kind 3 in the BDPK packs (`tools/bdpk.py`): 2,156 scripts in 23 packs. Names: `bhb_mo_*` = motion (moves, 1 big
shared one + many cutscene ones), `bhb_ai_*` = AI brains, `bhb_bo_*` = other. Levels pick them by id
(`CHAR_CREATE AI_SCRIPT / MOT_SCRIPT` in `.evd`).

## 2. File layout

Chunks: 4-char tag, u32 length, data padded to 8 bytes. **Bit 0 of the length set = the chunk holds more chunks**
(the real length is `len & ~1`). `PROG` is the one exception: bit 0 clear, but it always holds one `AAS2` chunk.

```
BHEV                      the script
  UPDT  build date text   ("Mon Jan 26 20:37:17 2004")
  NAME  script name       ("mo_hero")
  ATR   8 bytes           (0 for the hero, 6 for most AI brains; not decoded)
  DATA* a data table      NAME + PROG > AAS2 > IFDT (u32 type 0=int 1=float, u32 dims, u32 size...) + DATA (values)
  STAT* a state           STRT (on enter) / RUN (every frame) / STOP (on leave), each PROG > AAS2; then NAME
  SUB*  a subroutine      PROG > AAS2, NAME
  STOP  (once, top level)

AAS2 (one program)
  INFO  16 bytes          i32 -1, u16 extra int variables, ... (u16 at +0xC/+0xE of the loaded module = counts)
  CODE  the bytecode
  FTBL  float constants   (PSHFN n pushes FTBL[n])
  CTBL  strings           (PSHS n pushes the string at byte offset n; mostly debug text)
```

States, subs and data tables are numbered separately, in file order (`STATE 276` = the 277th `STAT`). Scripts call
them by these numbers. The hero script has 603 states, 156 subs and 398 data tables.

DATA tables are indexed `[character type][column]`. Example `Life_Stmina` (17 x 4: life, max life, stamina, max
stamina per character type) and 397 `Motion_*` tables (17 x 1: the animation id each character type uses for that
state). Bytes after the values are left-over build data; the game reads only `product(dims)` values.

## 3. The interpreter

| Address | Name | What |
|---|---|---|
| 0x96CC0 | AAS_Init | makes the AASExeMgr: one 0x200-slot stack (+0x800 = stack pointer, grows down) |
| 0x96CF0 / 0x96D20 | AAS_RunProgram / AAS_RunSubProgram | check the `AAS2` magic, run on a fresh / the current stack |
| 0xAB6C0 | AAS_Exec | sets up, calls the loop |
| **0xAB4E0** | **AAS_Interpret** | **the loop**: `op = code[pc]`; length from the table at **0x24D270** (12-byte rows: opcode, name pointer, length); operand = the next `length-1` bytes little-endian, top byte sign-extends; `pc += length`; `op == 0x0F` (END) stops; `op < 0x41` calls handler **0x24D640[op]**, else error 8 |
| 0x96CE0 | AAS_CallNative | `jmp [0x24B478 + n*4]`: game command n |
| 0xAB8C0 / 8E0 / 900 | AAS_ArgInt / ArgFloat / ArgString | read argument n of a game command (`stack[sp+n]`, n = 0 is the last pushed) |
| 0xAB920 / 930 | AAS_ReturnInt / ReturnFloat | a game command's result goes to I0 / F0 |

On entry the loop reserves two frames on the stack: `4 + extra ints` int slots and `4 + extra floats` float slots.
**Variables:** `I0` = the result register (every push, call and compare writes it), `I1` = switch value, `I2..I3`
spare, `I4...` = local variables of this block. Same for floats `F0...`. Errors are flags: 1 divide by zero,
2 stack overflow, 4 stack underflow, 8 bad opcode.

Persistent values live outside the VM and are reached by game commands:

| Store | Where | Commands |
|---|---|---|
| script-instance ints / floats (40 each) | runner +0x4C / +0xEC | Local_Get/Set (#25/#26), LocalF_Get/Set (#27/#28) |
| character ints (84) | char +0x5DB4 | CharVar_Get/Set (#31/#32). **43 = attack power x10, 44 = defence x10** |
| character timers (15) | char +0x5F08 | Timer_Get (#33), Timer_Reset (#36) |
| game-wide ints (story flags) | 0x2A8E04 table +0x100 | Global_Get/Set (#760/#759) |

## 4. Opcodes (all 65; names from the game)

"push" = put on the stack and also copy into I0. Operand sizes: C = 1 byte, S = 2 bytes, no letter = 4 bytes.

| Op | Name | Len | Does |
|---|---|---|---|
| 00 | NOP | 1 | nothing |
| 01 | POP n | 2 | drop n slots |
| 02 | PSHI n | 2 | push int variable n |
| 03 | PSHIN v | 5 | push int v |
| 04 | PSHF n | 2 | push float variable n |
| 05 | PSHFN n | 3 | push FTBL[n] |
| 06 | PSHS off | 3 | push a string (CTBL offset) |
| 07 | PSHID n | 3 | push the *number* of variable n (for `set`, `++`) |
| 08 | CAL n | 3 | n < 0x30: built-in op (below); n >= 0x30: game command n-0x30. Does not pop: a POPn follows |
| 09 | LBL | 1 | nothing (label) |
| 0A / 0B / 0C | JMP / JPZ / JPNZ a | 5 | pc = a (always / if I0 == 0 / if I0 != 0) |
| 0D | CMP_I1_IN v | 5 | I0 = I1 - v (switch-case test) |
| 0E | MV_I1_I0 | 1 | I1 = I0 (start of a switch) |
| 0F | END | 1 | stop |
| 10 | PSHCIN | 5 | nothing in this build (unused) |
| 11-18 | POP1..POP8 | 1 | drop 1..8 |
| 19-20 | PSHI0..PSHI7 | 1 | push int variable 0..7 |
| 21-28 | PSHF0..PSHF7 | 1 | push float variable 0..7 |
| 29 / 2A | PSHIN0 / PSHIN1 | 1 | push 0 / 1 |
| 2B / 2C | PSHINC / PSHINS v | 2 / 3 | push a 1- / 2-byte int |
| 2D / 2E / 2F | PSHFN0 / PSHFNP1 / PSHFNM1 | 1 | push 0.0 / 1.0 / -1.0 |
| 30 | PSHFNC n | 2 | push FTBL[n] |
| 31 / 32 | PSHSC / PSHIDC | 2 | short PSHS / PSHID |
| 33 / 34 | CMP_I1_INC / INS | 2 / 3 | short CMP_I1_IN |
| 35-3A | JMPC JMPS JPZC JPZS JPNZC JPNZS | 2-3 | short absolute jumps |
| 3B / 3C | BRA / BRAC | 3 / 2 | pc += offset (from the next instruction) |
| 3D / 3E | BRZ / BRZC | 3 / 2 | branch if I0 == 0 |
| 3F / 40 | BRNZ / BRNZC | 3 / 2 | branch if I0 != 0 |

The compiler uses 46 of the 65 (the long absolute jumps are never used; branches are relative).

**Built-in ops (CAL 0x00-0x2F)**, on a = second from top, b = top: 1 add, 2 sub, 3 mul, 4 div, 5 neg, 6 eq, 7 lt,
8 le, 9 gt, 10 ge, 11 ne, 12 set (variable a = b), 13-16 ++/-- (pre/post), 17 mod, 18 and, 19 or, 20 xor, 21 shl,
22 shr, 23 not, 24 logical not, 25 &&, 26 ||, 27 float->int, 28 int->float, 29-47 the same for floats (fadd ...
flnot). Integer results go to I0, float results to F0.

**Typical code** (from `SudeTachiAttack1`): `damage = 8 * CharVar(43) / 10`:
```
PSHINC 8 ; PSHINC 43 ; CAL CharVar_Get ; POP1 ; PSHI0 ; CAL .mul ; POP2 ; PSHI0 ; PSHINC 10 ; CAL .div ; POP2 ; PSHI0
```
An `if` is `<condition> ; BRZC else`. A `switch` is `<value> ; MV_I1_I0 ; CMP_I1_INC 1 ; BRZC case1 ; CMP_I1_INC 2 ...`.

## 5. How a script does things

| Task | How | Example |
|---|---|---|
| **Read buttons** | `Pad_Pressed(mask)` #6 (this frame), `Pad_Held(mask)` #5, `Pad_Released(mask)` #7. Bits are the key manager's (0x2A9BA8), remapped from the pad by 0x53BA0 | Attack 1 = bit **0x800**, Attack 2 = bit **0x200** (from `SUB nomal_attack`) |
| **Play an animation** | `Motion_SetB(layer, anim)` #588, `Motion_SetIfNew(layer, anim)` #107, `Motion_PlayLayer(layer, anim, a, b)` #85; hands `Motion_SetHands` #147; camera `Camera_SetAnim` #584. Anim = a global id (pack kind 0) or `Data_Get(Motion_X, Char_GetType(), 0)` | Attack1: `Motion_SetB(0, gym_HE_FT_NN_BD_Rstr_NT)` (right straight) |
| **Timing** | `Motion_GetFrame(layer)` #111, `Motion_FramePassed(layer, f)` #114, `State_Time()` #23, timers | "if frame >= 22 and <= 28" |
| **Deal damage** | `Attack_Set(a, b, damage, reach, c, partA, partB, from, to)` #703 adds a hit window (up to 8) to char+0x4A10. Damage is written as `N * power / 10` | Attack1: 8 x power/10, frames 22-28 |
| **Take damage** | the victim's own script reads `Hit_Count` #163, `Hit_Type/Level/Damage(i)` #164-166 and calls `Life_AddFrom(amount)` #1456 (0x97760 -> Life_AddWithSource 0x48DA0) | hero: `Life_AddFrom(-Hit_Damage(0) * 10 / CharVar(44))` (22 places) |
| **Branch** | BRZ/BRNZ on I0 after a compare | |
| **Change state** | `State_Change(n)` #19 (next frame), `State_Get` #21, `State_GetPrev` #22 | |
| **Call** | `Sub_Call(n)` #24 runs SUB n now on the same stack | `Sub_Call(AttackStart)` |
| **Data tables** | `Data_Get(table, i, j)` #347 | `Data_Get(Motion_SudeTachiKamae, Char_GetType(), 0)` |
| **Debug text** | #0, #227-#232 print: empty in the retail game, but the strings are still there and name many values ("Life", "MLife", "ImpactLevel", "aasChrSetMotionSpeed"...) | |

**Damage, start to end:** attacker's state -> `Attack_Set(... damage = N x power/10, frames ...)` -> the engine's
hit check makes a hit record on the victim (type, level, damage) -> victim's script -> `Life_AddFrom(-damage x 10 /
defence)`. Power and defence are character variables 43 and 44 (10 = 100 %), set by `SUB POWERUP` (power 6..30).
`Life_AddWithSource` (0x48DA0) is called **only** by script commands, so every hit in the game goes through
#1456/#1457/#1103/#1458 - one hook there covers all damage (tuning range "damage taken / dealt x").

## 6. Who runs what

- `bhb_mo_hero` (773 KB, 603 states, 156 subs, 398 data tables) is the move set of **every fighting character**:
  Derrick, soldiers, T'lan, Solus, Navi, the clones. `Char_GetType()` (#701, char+0x5AAE, 17 types) picks the row in
  each data table; type 0 = Derrick (row 0 animations are `gym_H*` hero ones).
- `bhb_ai_*` are brains: they pick targets, read distances (`DIST_E`, `E_DIR` debug labels), roll `Random`, and drive
  the body. Example `bhb_ai_g_enemy_sx_02`: 24 states (`GE_SX_Approach`, `GE_SX_Attack`, `GE_SX_GetAway`...), 76
  subs, a float table `GE_SX_AtkSetting_Tbl` (5 x 6 attack settings by distance).
- Cutscene `bhb_mo_s..._*` scripts play one animation and voice.

## 7. Check over every script

`python tools/aas2_dis.py game_files/romdata --check` (2026-11-14):

```
scripts 2156, failed 0, states 11992, subs 6613, code blocks 24317, instructions 2204031
opcodes used 46 of 65; game commands used 1009 of 1839 (highest #1835); named 87
```
Also run on `bhb_mo_hero` (295k lines of listing), `bhb_ai_g_enemy_sx_02`, `bhb_ai_g_enemy_at_01` and
`bhb_ai_s02a03_010_sykj_SX` (hon_under) without errors. Listings are not committed (game data).

## 8. Open

- Which pad button is which key-manager bit (0x800 = Attack 1, 0x200 = Attack 2 ...): needs the pad path
  0x22B50 -> 0x29BEB0 -> 0x53BA0 read, or one live look. (P4.6a)
- 920 used game commands still have no name (they have signatures in `config/aas2_natives.csv`). (P4.6b)
- Attack_Set arguments a, b, reach, c, and the part ids (1..22): what each means. (P4.6c)
- `Life_Stmina` row 0 says 400 life, the game showed 170 for Derrick: find who reads the table. (P4.6d)
