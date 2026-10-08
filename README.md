# Breakdown Recompilation

**Version 0.2.2** — early, incomplete release. Windows only.

A launcher that turns **your own copy** of the original Xbox game *Breakdown* (Namco, 2004) into a
native Windows build, using a fork of [xboxrecomp](https://github.com/Heromachine/xboxrecomp) to
statically recompile the game's code into C rather than emulating it instruction by instruction.

**No game data is included.** The first time you start it, it asks for your Breakdown (USA) disc
image and builds the game from it, on your computer.

> ### ⚠️ This is early and incomplete
>
> It is playable, but it is not finished. Known issues in 0.2.2:
>
> - In some rooms the background music plays a short repeating fragment instead of the music.
>   Turning the BGM slider down in the game's sound settings silences it.
> - Loading after dying and choosing Retry can take a long time.
> - Some textures appear a moment late.

## What you need

- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 11
- Your own Breakdown (USA) Xbox disc image (`.iso`)
- About 6 GB of free disk space for the one-time build
- An Xbox-style controller is recommended

## Running it

1. Download `Breakdown-Recompilation-v0.2.2-windows-x64.zip` from
   [Releases](../../releases) and extract it. Keep the whole folder together — `Breakdown.exe`
   needs the `tools` folder beside it. The one-time build is faster from a local disk than from a
   network share.
2. Run `Breakdown.exe`. Windows may warn that it is from an unknown publisher: choose
   **More info**, then **Run anyway**.
3. First time only: choose your disc image and press **Continue**. The game is built from it; this
   takes a few minutes and happens once (and once more after updating to a new release).
4. Press **Play**.

### Options

- **Brightness** — *Original*, in the middle, matches the original game. Left is darker, right
  lightens the dark parts.
- **Full screen** — borderless, with black bars on a widescreen monitor so the 4:3 picture keeps
  its shape. F11 or Alt+Enter switches at any time, in game as well.
- **Saved games folder** — where saves are kept. Choosing a new folder does not move existing saves.

Menus work with mouse, keyboard or controller (D-pad/left stick to move, A to choose, B to go back).

### Where things are kept

```
%LOCALAPPDATA%\BreakdownRecomp\
    settings.ini   the disc image you chose, and your Options
    work\          the extracted game and the build (delete it to rebuild)
    logs\          launcher, build and game logs -- include the newest with any bug report
%LOCALAPPDATA%\xboxrecomp\    saved games (unless you choose another folder)
```

## Building from source

The release is assembled on Linux and cross-compiled for Windows with Zig.

- `launcher-win/` — the Windows launcher (Dear ImGui on Direct3D 11). `fetch_toolchain.sh`
  downloads the embeddable Python, Zig, CMake/Ninja and Capstone; `build.sh` builds
  `Breakdown.exe`; `make_release.sh` assembles the release folder
  (`OUT=... XBOXRECOMP=/path/to/xboxrecomp ./launcher-win/make_release.sh`).
- `tools/regenerate.py` — the recompile pipeline the launcher runs on the user's machine (extract
  the ISO, lift the XBE with xboxrecomp, apply the seed list). `regenerate.sh` is the Linux wrapper.
  After regenerating, run `apply_spin_fixes.py`.
- `src/` — the host entry point, and manual overrides that take precedence over the generated
  code for specific functions.
- `launcher/` — a Linux launcher (C#); not released yet.

Nothing game-derived belongs in this repository; see `.gitignore`.

## Credits and licences

- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nznet — MIT
- [xemu](https://github.com/xemu-project/xemu) — GPL-2.0-or-later; xboxrecomp's APU and NV2A
  libraries are derived from it, and it was the ground truth for this work
- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [Capstone](https://www.capstone-engine.org/), [Python](https://www.python.org/),
  [Zig](https://ziglang.org/), [CMake](https://cmake.org/) and [Ninja](https://ninja-build.org/),
  bundled for the one-time build under their own licences

*Breakdown* is © Namco. This project is not affiliated with or endorsed by Namco or Microsoft.
