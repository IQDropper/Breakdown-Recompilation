# Captions (subtitles) in Breakdown

Short version: **the game already ships full, timed subtitles** for its story voice lines, in six languages. The
text, line breaks and on-screen times live in caption files inside the packs; each voice line's level event names
its caption by number. Worked out statically on 2026-11-14. Tool: `tools/caption_dump.py`.

## Where the text lives

Pack entries of kind 5 (`tools/bdpk.py` lists them), magic `CAP`:

| file | holds | where |
|---|---|---|
| `cap_<scene>_<area>_<lang>` | spoken lines for that part of the game (`cap_s09_axis_e`) | the area's pack (`area/axis/axis.pak`) |
| `cap_movie_<lang>` | lines for the movies | area packs |
| `cap_syst_<lang>` | menus, help lines, save messages | `common/usa.pak`, `bri.pak`, `fra.pak` ... |
| `cap_sndroom_<lang>` | Sound Room (extras) labels | |

Languages: `e` English (US), `b` English (UK), `f` French, `g` German, `i` Italian, `s` Spanish.
English story files: 22 (`cap_s01_opening_e` ... `cap_s10_ending_e`), about 1,400 caption ids, numbered 0-1718
across the game (ids are unique per story file; `cap_syst` has its own numbering).

## File layout

| offset | meaning |
|---|---|
| 0x00 | `CAP\0` |
| 0x04 | u32 entry count |
| 0x10 | u32 glyph table offset (0x20) |
| 0x18 | u32 index offset |
| 0x1c | u32 text offset |
| 0x20 | u16 glyph count, u16 **font texture id** (a `nut_font_...` texture), then glyph rectangles `{u16 x, u16 y, u8 w, u8 h, u16 0}` = where each letter sits in the font picture |
| index | per entry `{u32 text offset in 2-byte units, u16 caption id, u16 priority}` (priority 50 / 100 / 150; 254 = shown at a fixed spot) |
| text | 2-byte codes, below |

Text codes:

| code | meaning |
|---|---|
| `0x01NN` | letter NN: 0-9 digits, 10-35 A-Z, 36-61 a-z, 62+ punctuation and accents (numbered per font; the tool maps them by font id) |
| `0x0003` | space |
| `0x0001` | new line |
| `0x0006` + float | **wait this many seconds** with the current text on screen; a wait before any text is a delay |
| `0x0002` | clear the screen, next page |
| `0x000C` ... | insert a number (system text only) |
| `0x0000` | end |

Example, caption 1254 (`cap_s09_axis_e`): wait 2.95 s, "Hey, wake up." for 2.30 s, clear, "You're our last
chance." for 2.00 s. German has the same timings with German text.

## How a voice line gets its subtitle

1. The level event (`.evd`, `EV_TYPE_STREAM`) has `SOUND_STREAM = ds_s09c04_020_WU_VO_Hey` and
   `SOUND_CAPTION = 1254`. 504 stream events carry a caption.
2. `EvSound_Create` (0x2F2C0) stores the caption id. When the stream actually starts playing ("STREAM GO!!"),
   `EvStream_Update` (0x2EAC0) calls `Caption_Start(owner, 1254)` (0x1147D0).
3. `Caption_Start` looks the id up in the caption file loaded for the current language (`DAT_003EA524` = language
   slot) and runs it on its own timer: the waits in the text decide when pages change. It is **not** tied to the
   audio position, only started at the same moment. The timer stops while the game is paused.
4. While playing, `Caption_SetHidden` (0x114A70) hides the subtitle if the sound's source is too far away; when the
   stream is stopped early `Caption_FadeOut` (0x114920) and `Caption_Stop` (0x114990) remove it.
5. Other callers of `Caption_Start`: `Movie_PlayXmv` (movie subtitles) and several scene functions (0x60E00,
   0xAEF50, 0x104550, 0x113E20, 0x115800, 0x11F70). They start the 959 English caption ids that no level event
   uses, so cutscene and movie lines are covered too.

One stream can be a whole conversation: `ds_s09c04_020_WU_VO_Hey` is 49.7 s long and its caption 1254 is one
page sequence; longer ones (1262, 1277) carry four or more timed pages.

## What has no subtitle

- **Combat barks and grunts** (656 of the 1,240 voice-bank lines: `ds_ARMEDTLAN_unari01`, `ds_CY_Notime`,
  `ds_WU_Anyway` ...) and all bank-sound voices (`snd_hero01v` ...) are played by code with no caption id.
  Some have matching text in caption files (for example caption 1255-1261 "Are you crazy!?", "You bastard!" are
  wired to stream events in the axis area), most do not.
- 9 voice lines used by level events have no `SOUND_CAPTION`; 39 caption ids named by events have no text in the
  English files; 49 stream names in events do not exist in the banks at all (cut lines).

## For real subtitles (accessibility)

- The data is there and timed. `python tools/caption_dump.py game_files/romdata --srt-ready out.csv` writes one row
  per page: caption id, stream name, file, delay, hold seconds, text. That is enough to make `.srt` files or an
  on-screen subtitle layer keyed by stream name.
- Open question: whether the Xbox game shows these captions by default or only with an option. The option strings
  do not mention subtitles, and `Caption_Start` returns early when the language slot's "on" flag
  (`DAT_003EA514[slot]`) is zero. Checking that flag at run time is the next step.
- Gaps to fill for full subtitles: speaker names (only in the stream name: `NV` Navi?, `WU`, `RT`, `CY`, `SX`, `RX`
  ...), barks (needs a name -> text table we write), and sound captions ("[gunfire]") which the game never had.
- Unread punctuation glyphs: 73 and 77 in the English caption font (77 appears before "Project"); shown as `{73}`.

## Functions named (config/function_names.csv)

`Caption_Start` 0x1147D0, `Caption_FadeOut` 0x114920, `Caption_Stop` 0x114990, `Caption_SetHidden` 0x114A70.
