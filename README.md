# Breakdown Recompilation (native PC)

Fan / preservation project: a **static recompilation** of the original Xbox game **Breakdown** (Namco / Pandemic, 2004) to a native Windows build using [xboxrecomp](https://github.com/apexJCL/xboxrecomp) and [xboxrecomp-cli](https://github.com/apexJCL/xboxrecomp-cli).

**This repository ships tooling and source only — never the game.** You must own a legitimate copy and supply your own dump.

Share / clone: https://github.com/IQDropper/Breakdown-Recompilation

---

## Legal / ownership disclaimers (please read)

- **Breakdown**, its name, characters, artwork, audio, and all related trademarks and copyrights are the property of their respective rights holders (historically associated with Pandemic Studios, Namco, Microsoft, and successors). This project is **not affiliated with, endorsed by, sponsored by, or partnered with** any of them.
- This is an **unofficial fan / technical / preservation** effort. It is **not** an official remaster, remake, re-release, or licensed port.
- **No game files, ISOs, XBEs, ROMs, keys, cracks, or copyrighted assets are distributed** in this repository. Git is configured to ignore `game_files/`, generated recomp output, builds, and disc dumps. **Do not open PRs that add them.**
- You must **own the game** (physical or digital through legitimate channels) and only use dumps you are authorized to make for personal use under the laws of your jurisdiction.
- Reverse engineering / recompilation for interoperability or personal use may be restricted or allowed depending on where you live. **This README is not legal advice.** If you need legal advice, consult an attorney.
- The maintainers provide this code **as-is, without warranty**. Use at your own risk. We are fans who want this game remembered and revisited — preferably with a proper official release someday.

If a rights holder wants changes or removal, open an issue or contact the repo owner; we will cooperate promptly.

---

## What "works" right now

This fork tracks a **working native Windows build path** used for PC playtesting (including menus / save flow on mid-range GPUs such as a GTX 1660 Ti). Texture flicker is the open image bug; see docs/flicker-investigation.md. The executable defaults RECOMP_D3D11_OCC=sync (same as run_native.cmd) so a direct launch does not leave lights and overlays on deferred visibility tests. Rebuild to pick that up. The first clean build on a new machine may crash until overrides / seeds match your dump — that is normal for this class of port.

**Others can run it** after:

1. Cloning this repo
2. Running setup once (script below)
3. Placing **their own** `game_files/` dump
4. Building / launching

---

## Quick start (Windows)

### Option A — helper script (recommended)

From a Command Prompt or PowerShell:

```bat
git clone https://github.com/IQDropper/Breakdown-Recompilation.git
cd Breakdown-Recompilation
SETUP.cmd
```

`SETUP.cmd` will:

- Enable `git config --global core.longpaths true` when possible
- Remind you to use a short checkout path
- Run `breakdown setup` (venv, llvm-mingw, toolkit — downloads are sha256-pinned)
- Run `breakdown doctor` and report what is missing
- Print where to put `game_files/` and how to build / run

### Option B — manual

```bat
git config --global core.longpaths true
git clone https://github.com/IQDropper/Breakdown-Recompilation.git
cd Breakdown-Recompilation

REM Prefer: breakdown.cmd   or:   py -3 breakdown.py
breakdown setup
breakdown doctor
```

Put your dump here (gitignored):

```text
game_files/
  default.xbe
  ... remaining disc files (romdata/, stream/, etc.)
```

Then:

```bat
breakdown all
```

First successful Windows binary:

```text
build-win\breakdown_recomp.exe
```

Or, if you already have a built `build-win\` and your dump is present:

```bat
run_native.cmd
```

---


## Controls (keyboard / mouse / pad)

See [CONTROLS.txt](CONTROLS.txt). `SETUP.cmd` runs `APPLY_INPUT_PATCHES.cmd` after toolkit setup so mouse capture is wired. Modern default: mouse look, LMB/RMB punches (LT/RT). A controller still works.

## Display (aspect and resolution)

See [docs/widescreen-hires.md](docs/widescreen-hires.md) and [docs/video-options.md](docs/video-options.md). The game frame stays 4:3. `run_native.cmd` asks for a 2x internal resolution and a 16:9 window that letterboxes that frame (HUD is not stretched, and the camera is not wider). Hor+ is not in this toolkit pin. `SETUP.cmd` runs `APPLY_DISPLAY_PATCHES.cmd` so the D3D11 window opens at that size and exclusive fullscreen can switch the display mode. Rebuild after the patch. In-game, F10 opens Video (internal resolution, window size, letterbox aspect, present filter, windowed, borderless, or exclusive). Apply writes `breakdown_video.cfg` beside the exe. A set `RECOMP_*` variable still overrides that file, so `run_native.cmd` keeps its launch. A direct launch with no variables and no config file stays 640x480, scale 1.

## Common commands

| Command | Purpose |
|--------|---------|
| `breakdown setup` | Once: `.venv`, llvm-mingw, toolkit (pinned hashes) |
| `breakdown doctor` | What this machine has / what to do next |
| `breakdown all` | Analyze → recomp → build |
| `breakdown analyze` / `breakdown recomp` | Regenerate `src/recomp/gen/` (do **not** edit gen by hand) |
| `run_native.cmd` | Launch helpers for the native exe |

Hand overrides live in `src/recomp_manual.c`. Missed function starts go in `config/seed_functions.json`. Settings are in `game.toml`. Upstream CLI docs: `docs/porting.md` / `docs/manifest.md` inside the toolkit after setup.

**Tip:** Keep the checkout on a short path (for example `D:\Emulation\Breakdown_Recomp` or `C:\g\breakdown`). Long OneDrive paths + Windows MAX_PATH still hurt toolchains even with longpaths enabled.

---

## What is intentionally not in git

See `.gitignore`. In particular:

- `game_files/` — your disc dump
- `src/recomp/gen/` — generated from your dump
- `build-win/`, `.venv/`, `third_party/`, `external/` — local toolchain / build trees
- Session logs / bench artefacts

---

## Credits

- Original game: Pandemic Studios / Namco (2004, Xbox)
- Recomp stack: [xboxrecomp](https://github.com/apexJCL/xboxrecomp), [xboxrecomp-cli](https://github.com/apexJCL/xboxrecomp-cli)
- Upstream project template: [Heromachine/Breakdown-Recompilation](https://github.com/Heromachine/Breakdown-Recompilation)
- This fork: working-tree sync and PC playtest notes for sharing with other fans who **own the game**

---

## License / contribution note

Tooling and original code in this repo are provided for educational and preservation discussion among legitimate owners. Do not submit copyrighted game assets. By contributing, you confirm your patches do not include dumps, ISOs, or generated game-derived blobs that belong in `game_files/` or `src/recomp/gen/`.
