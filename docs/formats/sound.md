# Sound in Breakdown

How the game stores, picks and plays sound. Worked out statically on 2026-11-14 (no game run), from the game files,
the decompiled code and `common/nus_config_x.txt`. Tools: `tools/sound_dump.py` (banks), `tools/caption_dump.py`
(subtitles, see [captions.md](captions.md)), `tools/evd_dump.py` (level events).

## Short version

- Two kinds of sound. **Streams** are long files read from disk while they play: music, longer effects and every
  voice line. **Bank sounds** are short effects (footsteps, guns, doors, grunts) kept in memory inside the level packs.
- Levels say what plays. Each level's event file (`.evd`) has `EV_TYPE_STREAM` events (a stream by name) and
  `EV_TYPE_SOUND` events (a bank number + sound number), with a place in the world, a near/far range and loop mode.
  Code also plays sounds directly (gunfire, footsteps, enemy barks, menus).
- "3D" is done by the game itself: it turns distance into volume and direction into left/right pan. Reverb and echo
  come from the Xbox sound chip's effects processor, using presets in `nus_config_x.txt`.
- Everything is Xbox ADPCM (4-bit, 64 samples per 36-byte block). Streams are 44.1 kHz; bank sounds use many rates
  (2-44.1 kHz, mostly 18 kHz).
- Sound engine name: "NUS" (Namco). Programmer tags in debug text: `makino` (sound), `kondoh` (scripted scenes).

## 1. Stream banks: `stream/bgm`, `se`, `voice`

Three pairs: `.sth` = table of contents, `.stw` = the sounds back to back.

| bank | sounds | total | format | mixer bus |
|---|---|---|---|---|
| bgm (music) | 230 | 164 min | ADPCM, 228 at 44.1 kHz + 2 at 48 kHz; 225 stereo | 28 BGM |
| se (long effects) | 273 | 47 min | ADPCM 44.1 kHz mono (1 PCM file, 2 stereo) | 30 SE (6 on 29) |
| voice | 1,240 | 90 min | ADPCM 44.1 kHz mono | 29 VOICE |

(Older notes said 224 / 270 sounds; those counted RIFF headers. The `.sth` counts above are what the game reads.)

### `.sth` layout

| offset | meaning |
|---|---|
| 0x00 | `strmpk` magic |
| 0x08 | bank file name (`bgm.stw`) and leftover pointers from the authoring tool (ignored) |
| 0x28 | u32 sound count |
| 0x2c | u32 group count |
| 0x30 | u32 offset of the group table: count x 16 bytes `{u32 first sound, u32 first name offset, char prefix[8]}` (`ds_bg0`, `ds_se3`, `ds_s01` ...) |
| 0x34 | u32 size of the name block |
| 0x38 | u32 offset of the name block: NUL-separated names, sound *i* = name *i* |
| 0x3c | u32 offset of the start table: u32 per sound, its byte offset in `.stw` (it ends where the next starts; the last ends at the end of the file) |

### Each sound in `.stw`

A RIFF/WAVE file: `fmt ` (0x14 bytes; codec 0x69 Xbox ADPCM, block align 36, 64 samples per block), then a Namco
`NXBF` chunk, then `data`. `NXBF` (0x34 bytes) = `'NXBF'` + 12 u32:

| u32 | meaning | values seen |
|---|---|---|
| 1 | stream buffer size | 0x1000 |
| 2 | data bytes | = data chunk |
| 3 | channels | 1 / 2 |
| 5 | loop start in samples; 0xFFFFFFFF = plays once | 104 music + 11 effects loop |
| 6 | sample rate | 44100 / 48000 |
| 7 | volume x1000 (1000 = full, 629 is about -4 dB) | music mostly 629, voice 590 |
| 8 | mixer bus (see section 6) | 28 / 29 / 30 |
| 9 | 1 on most, 0 on 87 voice lines | meaning open |
| 10 | 0 on music, 127 on effects and voice | likely effect send or priority (open) |
| 12 | samples per block | 64 |

### How the game finds a stream (code)

- `Stream_Init` (0x2F8C0) loads the three `.sth` files at boot (`File_ReadWhole` 0x17F260).
- `Stream_OpenWav` (0x2F7F0) picks the bank by name: `ds_bg*` -> bgm, `ds_se*` / `ds_nosound*` -> se, anything else ->
  voice. `StreamPak_FindOffset` (0x2F740) matches the first 6 letters against the group table, then walks the names
  (case-insensitive) and returns the `.stw` offset.
- Not found -> it falls back to a loose file `stream/<name>.wav`. The game files have none, so those play nothing.
  49 stream names used by level events are missing from the banks (cut lines such as `ds_s03c01_010_NV_VO_Hidein`,
  music `ds_bg0030`, `ds_bg4020`, `ds_bg5066`).
- Names whose letters 2-4 are `rnd` get a random digit 1-3 put in place 5 (`Stream_Play`).

### Stream slots and music switching

The stream manager (global at 0x29F69C) has 4 slots of 0x58 bytes. `Stream_ClassifyName` (0x1CEF0) sorts a name:

| name | slot | priority |
|---|---|---|
| `ds_nosound` | mono | 99 |
| `ds_bg*` (music) | **slot 0, stereo** | 1 |
| `ds_se*` | mono | 10 |
| `ds_sNN*` (story voice, two digits) | mono | 1 |
| other voice (barks) | mono | 20 |

- Music always takes slot 0. `Stream_PlayMusic` (0x1D500) stops whatever music is playing (the
  "StereoStream Killed" debug line) and starts the new one. So **music switching = a level event or script starting
  another `ds_bg` stream**; there is no cross-fade table. A stream event's `SOUND_FADEIN` / `SOUND_FADEOUT`
  (seconds, fade-out default 4.0) shape the change.
- Slots 1-3 hold mono streams (voice, long effects). `Stream_Play` (0x1D810) asks for a free slot by priority and
  starts the stream. `Stream_PlayDefault` (0x1D7F0) is the wrapper scripts, movies and `Game_Update` call.
- 104 of 230 music tracks loop (NXBF loop start), most from the very start.

Music names: `ds_bgNxxx`, where N is roughly the game chapter (0 menus/title, 1-9 and `a` story parts;
`ds_bg9xxx` play in the axis area, the last chapter).

## 2. Bank sounds inside the packs

Pack entries of kind 4 (`snd_...`, see `tools/bdpk.py`) hold `XHDp` sound banks. 204 distinct banks, 1,026 copies
across packs (each level part repeats the banks it needs). `python tools/sound_dump.py game_files/romdata --packs`.

| offset | meaning |
|---|---|
| 0x00 | `XHDp`, u8 1, u8 1 |
| 0x06 | u16 **bank number** (what `.evd` `SOUND_BANK` uses; not the pack entry id) |
| 0x08 | u16 sound (tone) count |
| 0x0a | u16 header size = where wave data starts |
| 0x0c | u32 wave data bytes |
| 0x10 / 0x14 | tone table offset (0x20) / size; one 0x58-byte record per tone: pan 0x40 at +4, volume 0x7f, **mixer bus at +0x0b**, 3D distance floats further on (2000.0, 30.0, 1000.0 seen) |
| 0x18 / 0x1c | wave table offset / size; one 0x114-byte record per tone: WAVEFORMATEX, then data offset, data bytes, loop flag, loop start, loop end, rate |

Tone *i* plays wave *i*. Rates: 18 kHz (4,977 of 9,363 copies), 16 kHz, 8 kHz, 22.05 kHz, 24 kHz, 20 kHz, 44.1 kHz
and a few 2-17 kHz. 17 sounds are plain PCM, the rest ADPCM.

### Names

The XBE has a name table for every bank and tone (VA 0x227848: 282 banks, 1,904 tones):
`python tools/sound_dump.py game_files/Default.xbe --names`. A level event called `NUD_TONE_QUAKE_L` is bank 70
(`QUAKE`) tone 2 (`_L`). `SndBank_PlayTone` (0x23DA0) checks a bank/tone pair against this table
("illigal bank/tone requested" if wrong), then plays it if the bank's pack is loaded ("Sound Pack %d Status Not
Ready" if not). Bank data is wired up when a pack loads by `Data_SetupSounds` (0x1B1F0).

## 3. How a sound gets picked

1. **Level events** (`.evd`; 583 `EV_TYPE_SOUND`, 947 `EV_TYPE_STREAM` across all areas). `EvSound_Create`
   (0x2F2C0) reads these fields:

   | field | meaning | default |
   |---|---|---|
   | `SOUND_BANK`, `SOUND_TONE` | bank sound (section 2) | |
   | `SOUND_STREAM` | stream name (section 1) | |
   | `SOUND_VOLUME` | 0-1 | 1.0 |
   | `SOUND_PITCH` | | 0 |
   | `SOUND_NEAR`, `SOUND_FAR` | full volume inside near, silent past far | 30, 80 |
   | `SOUND_LOOP` | 0 once, 1 loop while in range, 2 one-shot then delete, 3 wait for end | 0 |
   | `SOUND_PRIORITY` | stream priority | 0 |
   | `SOUND_CAPTION` | caption id for the subtitle (see captions.md) | -1 none |
   | `SOUND_FADEIN`, `SOUND_FADEOUT` | seconds | 0, 4.0 |
   | `SOUND_EFFECT` | if set the event never updates (switched off) | -1 |
   | `POSITION_X/Y/Z` | world spot; all zero = not positional (plays flat) | |
   | `MODE` | 1 armed; rules set it to 2 to play, 3/4 to stop (as with other events) | |

   A small table at 0x20F8D0 overrides the far range for 7 banks (fire, rico, noise ...).
   `EvSound_Update` (0x2E740) runs bank sounds, `EvStream_Update` (0x2EAC0) runs streams; level rules
   (`EV_TYPE_OBSERVE_*`) start them by setting `MODE`, for example "when the player enters this box, play
   `ds_s09c04_020_ND_VO_Screams`".
2. **Code**: player, enemies and weapons call bank sounds directly (`SE_FIGHT_VOICE_FIND`, `SE_FIGHT_HIT_PUNCH`
   ... tables near 0x212000; voice barks such as `ds_CY_Notime`, `ds_ARMEDTLAN_unari01` from tables at 0x24A1F8).
   Scripted scenes (`ko_ncc_0`, 0x109AA0) and movies start streams through `Stream_PlayDefault`.

## 4. 3D sound

The game does it in software, per sound, every frame:

- `EvSound_DistanceVolume` (0x2E3B0): full volume up to `SOUND_NEAR`, a straight line down to silence at
  `SOUND_FAR`.
- `EvSound_Pan` (0x2E410): left/right pan -64..+64 from the angle to the camera; sounds behind are mirrored.
- Streams placed in the world get the same treatment (`Sound_SetStreamVolume` 0x1A3E20, `Sound_SetStreamPan`
  0x1A3DF0), and their subtitle is hidden when the player is too far (`Caption_SetHidden`).
- Bank sounds also go through the DirectSound 3D path: `Sound_UpdateFxAnd3D` (0x1A3030, called from `Game_Main`)
  updates 256 voices and the I3DL2 listener (room reverb). `nus_config_x.txt` sets the 3D ranges:
  distance 10, attenuation 1, doppler 1 (limit 2), priority rate 80.
- `Sound_CommitDeferred` (0x1A1830, from `Sound_Update`) pushes all changes to DirectSound once per frame.

## 5. Mixer setup: `common/nus_config_x.txt`

Written by the sound team's spreadsheet tool (`Nus_Prog.xls` 1.61c, 2003-11-26). Japanese comments translated here.

**32 buses** `BusN = parent, volume (0-127), pan (64 = centre), effect`:

| bus | effect / role |
|---|---|
| 0 | master output |
| 1 | reverb "hall" (preset says STONEROOM) |
| 2 | reverb "studio large" (CONCERTHALL) |
| 3 | reverb "studio medium" (DEFAULT2) |
| 4 | reverb "studio small" (STONECORRIDOR) |
| 5 | reverb "room" (ALLEY) |
| 6 | delay (echo) |
| 7 | flanger |
| 8-27 | plain, feed bus 0 |
| 28 | group BGM (music), volume 127 |
| 29 | group VOICE, 127 |
| 30 | group SE (effects), **110** (a bit quieter) |
| 31 | group SYSTEM SE (menus), 127 |

**36 effect presets** `FXn`: type 0 = I3DL2 reverb (13 numbers = room, room HF, room rolloff, decay time, decay HF
ratio, reflections, reflections delay, reverb, reverb delay, diffusion, density, HF reference), 1 = chorus,
2 = echo, 3 = distortion, 4 = flanger. FX1-5 are the five reverbs above, FX6 delay, FX7 flanger, FX8-36 the
standard Xbox I3DL2 list (padded cell, cave, hangar, sewer pipe, underwater ...) plus chorus, echo, distortion,
overdrive, flanger. The effects run on the Xbox sound chip's DSP using the effects image `common/dsstdfx.bin`
(the XDK's standard effects, 19 KB).

**Voice limits** `GROUPn = max sounds at once`: 0 unlimited (200), 1 hero voice 16, 2 giant pole 8, 3 SOCOM shell
casings 2, 4 MP5 casings 3, 5 enemy MP5 shots 4, 6 swarming enemies 4, 7 T'lan barrier 1, 8 floating enemy 4,
10 noise loops 6, 11 ricochets / flying bullets 14.

## 6. Volume groups and settings

Every sound carries a bus number (NXBF field 8 for streams, tone byte +0x0b for bank sounds): 28 music, 29 voice,
30 effects, 31 menu sounds. The Options menu has BGM, SE and VOICE sliders (`BGM_slider`, `SE_slider` ...;
debug lines "BGM VOLUME : %d", "VOICE VOLUME : %d") that set those group buses. Hero and enemy voices sit on 29,
so the voice slider also covers grunts and barks.

## 7. Still open

- Exact meaning of NXBF fields 9 and 10 and the `XHDp` tone byte 0 (0xFF on most tones; maybe a voice-limit group).
- Which code reads `nus_config_x.txt` and `dsstdfx.bin` (strings at 0x20C123 / 0x20C103 have no direct reference;
  likely a table) and which areas switch reverb presets and how.
- Where the 959 caption ids not used by level events are started (scripts, movies; see captions.md).
- A run with sound logging would confirm the slot/priority rules and the music changes per area.

## Functions named (config/function_names.csv)

`Stream_ClassifyName` 0x1CEF0, `Stream_PlayMusic` 0x1D500, `Stream_PlayFade` 0x1D730, `Stream_PlayDefault` 0x1D7F0,
`Stream_Play` 0x1D810, `Stream_StartPlaying` 0x1D0E0, `StreamPak_FindOffset` 0x2F740, `Stream_OpenWav` 0x2F7F0,
`Stream_Init` 0x2F8C0, `File_ReadWhole` 0x17F260, `EvSound_Create` 0x2F2C0, `EvSound_Update` 0x2E740,
`EvStream_Update` 0x2EAC0, `EvSound_DistanceVolume` 0x2E3B0, `EvSound_Pan` 0x2E410, `SndBank_PlayTone` 0x23DA0,
`Sound_CommitDeferred` 0x1A1830, `Sound_UpdateFxAnd3D` 0x1A3030, `Sound_SetStreamVolume` 0x1A3E20,
`Sound_SetStreamPan` 0x1A3DF0, plus the caption functions in captions.md.
