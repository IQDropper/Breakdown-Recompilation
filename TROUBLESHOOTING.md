# Troubleshooting

Smoke-test and launch failures when the recomp cannot see your dump. Setup and build commands stay in [README.md](README.md).

## Missing ISO / `game_files`

Startup reads the extracted dump. It `fopen`s the relative path `game_files/default.xbe`, then sets the game directory to `game_files` (`src/main.c`; `game.toml` `[xbe] path` and `[data] game_files`).

When that file is not in the process working directory, startup stops:

- stderr: `Cannot open XBE: game_files/default.xbe`
- dialog title `Recomp`: `Failed to load default.xbe.` and `Place the game files in the 'game' subdirectory.`

The dialog names a `game` subdirectory. The path the exe opens is `game_files/default.xbe`. A short smoke (`run_smoke.cmd` from [PR #10](https://github.com/IQDropper/Breakdown-Recompilation/pull/10)) treats `Cannot open XBE` and `Failed to load default.xbe` as a failed load of the extracted files.

The same strings show up when the extract is at the clone root and the exe was started with another cwd. Explorer sets cwd to `build-win` for `build-win\breakdown_recomp.exe`, so the open looks for `build-win\game_files\default.xbe`.

## Where files go

Extracted dump the exe reads, at the clone root (what `SETUP.cmd` checks):

```text
game_files/default.xbe
game_files/romdata/
game_files/stream/
```

Title id for Breakdown (USA) is `0x4E4D0009` (`game.toml`).

Disc image for `FIRST_RUN.cmd` / `first_run.py` `discover()` ([PR #10](https://github.com/IQDropper/Breakdown-Recompilation/pull/10)). Files only, in these two folders, not subfolders. Name class is checked before folder, so `Breakdown (USA).iso` next to `FIRST_RUN.cmd` beats a stray `.xiso` in `build-win`. Inside one class, `build-win` wins. `Breakdown (USA).*` leads other `Breakdown*` names. An `.iso` whose name does not start with `Breakdown` is left for a pasted path.

1. `build-win\Breakdown*.iso` — same folder as `breakdown_recomp.exe`
2. `Breakdown*.iso` next to `FIRST_RUN.cmd` (clone root)
3. `build-win\*.xiso`
4. `*.xiso` next to `FIRST_RUN.cmd`
5. `build-win\*.xiso.iso`
6. `*.xiso.iso` next to `FIRST_RUN.cmd`
7. A path you type or paste, when 1–6 match nothing

`FIRST_RUN.cmd` ships with that PR. When the script is in your checkout, use it. Otherwise import from the clone root (the command `overnight.ps1` uses).

## Fix

1. Put an owned `Breakdown (USA).iso`, `.xiso`, or `.xiso.iso` in `build-win\` or next to `FIRST_RUN.cmd`, or keep any path you will pass in.
2. Extract. With the wizard, run `FIRST_RUN.cmd` from the clone root and press Enter on the suggestion. It imports, checks title id `0x4E4D0009` plus `romdata` and `stream`, then smokes if `build-win\breakdown_recomp.exe` exists. Without the wizard: `breakdown import "path\to\Breakdown (USA).iso"`. If `game_files\` already holds a different dump, run `breakdown import --replace` yourself.
3. Launch with a script that sets cwd to the clone root, then starts the exe: `run_native.cmd`, `run_native_cpu.cmd`, `run_native_nortt.cmd`, `run_native_occdefer.cmd` (`cd /d %~dp0`), or `run_smoke.cmd` (cwd is the repo). `[SKIP] native smoke` means the exe is not built yet: `breakdown all`, then one of those scripts.

## Do not commit dumps

ISOs, XISOs, XBEs, and `game_files/` must never be committed. `.gitignore` ignores them. See the README ownership rules.
