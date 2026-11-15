# PC audio: what is missing versus the Xbox

2026-11-14, read from the code only (no game run). Sources: the game's sound setup ([sound.md](../formats/sound.md)),
the toolkit's APU model `external/xboxrecomp/src/apu/` (read only), and `src/main.c`.

## How sound reaches the speakers on PC

The game links Microsoft's Xbox DirectSound library into its own program, so that library was recompiled with the
game (`game.toml` sweeps the `DSOUND` section). It talks to the Xbox sound chip (the "APU") through hardware
registers. On PC those registers are trapped and fed to a software model of the chip taken from xemu
(`mcpx_apu_init_standalone`, on by default on Windows: `RECOMP_AC97_READY`). The chip has three parts:

| part | job on the Xbox | on PC |
|---|---|---|
| VP, voice processor | decode ADPCM, change rate, volume envelopes, low-pass filter, HRTF 3D, send each voice to up to 8 of 32 "mix bins" | modelled (`apu_vp.c`) |
| GP, global processor (a DSP) | runs the effects program `dsstdfx.bin`: reverb, echo, chorus, flanger, sub-mixes; mixes the 32 bins down | **stub** (`apu_dsp.c`) |
| EP, encode processor (a DSP) | final stereo / 5.1 / Dolby output | **stub**, pass-through |

The log line "[APU] DSP GP/EP initialized (STUBBED - passthrough mode)" is exactly this.

## The gaps

| # | what | Xbox | PC now | effect in Breakdown |
|---|---|---|---|---|
| 1 | Reverb, echo, flanger | GP DSP runs the presets from `nus_config_x.txt` (FX1-7 on buses 1-7, 36 presets) | GP not run; `SetEffectData` / `SetI3DL2Listener` calls land nowhere | rooms sound dry: no hall / corridor / cave reverb, no delay or flanger effects |
| 2 | Effect sends mixed dry | sends to bins 1-7 go through the effect only | `apu_dsp.c` sums **every** bin straight to the output (even bins left, odd right) | anything sent to an effect bus is heard un-effected, and a sound sent to both dry and effect buses is louder than intended; left/right of effect buses is a guess |
| 3 | Final mix | EP mixes bins to stereo or 5.1 with proper levels | even/odd sum, then clamp to -1..1 | no surround; possible clipping when many sounds play (no headroom; `SetMixBinHeadroom` ignored) |
| 4 | DSP replies | GP answers commands and reports | a "doorbell ack" (`RECOMP_APU_DSP_ACK=auto`) says every command worked | `GetEffectData` reads back nothing real; harmless unless the game reads levels back (not seen) |
| 5 | Rate change | VP uses high-quality resampling | linear interpolation (libsamplerate stubbed), output always 48 kHz | bank sounds are mostly 8-24 kHz (18 kHz most common), so they get the roughest conversion: slightly dull or aliased highs. Streams are 44.1 kHz and fine |
| 6 | 3D | the game's own distance volume and pan (software) plus DirectSound 3D / HRTF on bank voices | game side fully works (it is game code); HRTF filter is modelled in the VP, but 3D voices land in bins that only the even/odd sum keeps | direction and distance should be right; HRTF "behind you" colouring and doppler not checked |
| 7 | Voice limits | GROUP limits (16 hero voices, 2 shell casings ...) are game code | same | no gap |
| 8 | Group volumes (BGM / SE / VOICE sliders) | game buses 28-31, game code | same | no gap |

## What the game asks for (evidence)

- `Sound_UpdateFxAnd3D` (0x1A3030) loops over 32 effect slots each frame and, by type, calls
  `IDirectSound_SetI3DL2Listener` (reverb) or reads and writes effect parameters (`CDirectSound_GetEffectData` /
  `SetEffectData`, types 1 chorus, 2 echo, 4 flanger). The DSOUND library also has
  `CDirectSound_DownloadEffectsImage` (0x1D16B7) for `dsstdfx.bin`.
- `nus_config_x.txt` names the reverb buses after rooms (hall, large / medium / small studio, room) and lists 36
  presets, so the sound team expected reverb to be heard.
- Toolkit side: `apu_dsp.c` header ("GP effects processing is skipped"), the `mcpx_apu_dsp_frame` mix-down, and the
  `apu_vp.c` note that libsamplerate is stubbed and replaced by linear interpolation.

## What it would take

| gap | approach | size |
|---|---|---|
| 1-2 reverb and effects | **A.** Port xemu's DSP56300 emulator and run the real `dsstdfx.bin` (exact, about 3,000 lines, CPU cost). **B.** Recognise the effect calls at the DirectSound level and run our own reverb / echo / flanger on bins 1-7 using the I3DL2 numbers the game passes (close enough, cheaper). B fits a recompilation better | large (A) / medium (B) |
| 2-3 mix-down | Use the bin layout DirectSound sets up (bins 0-5 speakers, the rest effect sends): mix speakers properly, drop or effect the sends, add headroom instead of a hard clamp | small, once 1 is decided |
| 5 resampling | Swap the linear step for a windowed-sinc or polyphase resampler in `voice_resample` (libsamplerate or a small built-in filter) | small |
| 6 3D check | One run with `RECOMP_APU_TRACE` to see which bins 3D voices use and whether HRTF entries are loaded | small (needs a game run) |
| subtitles | Not an audio gap: the game's own captions can be drawn or exported (captions.md) | |

All toolkit changes belong in `external/xboxrecomp` (not this repo); a Breakdown-only option could live in
`src/` as an override. Nothing here was changed, only read.
