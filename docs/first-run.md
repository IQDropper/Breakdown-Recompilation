# First run (your own Breakdown ISO/XISO)

This page is the turnkey path for someone who cloned the repo and already owns Breakdown. The scripts never download, fetch, or ship a disc image. Git ignores `game_files/`, `*.iso`, and `*.xiso`.

## Where to put the disc image

Keep the file inside the checkout. The wizard does not ask for a path on another drive as the normal step. It resolves a suggestion in this order (files directly in the folder, not in subfolders):

1. `build-win\Breakdown*.iso` — same folder as `breakdown_recomp.exe`
2. `Breakdown*.iso` next to `FIRST_RUN.cmd` — one folder up from that exe
3. `build-win\*.xiso`
4. `*.xiso` next to `FIRST_RUN.cmd`
5. `build-win\*.xiso.iso`
6. `*.xiso.iso` next to `FIRST_RUN.cmd`
7. A path you type or paste, when none of those names are in those two folders

`Breakdown (USA).iso` leads other `Breakdown*.iso` names in the same folder. An `.iso` whose name does not start with `Breakdown` is not auto-detected.

Press Enter to use the suggestion. If several images are present, type the number of the one you want. If neither folder has an image yet, copy yours into one of them and press Enter so the script looks again.

Pasting a full path is step 7, including an already extracted folder that contains `default.xbe`.

## What `FIRST_RUN.cmd` does

1. Asks for the disc image as above and saves the path you confirmed.
2. Runs `SETUP.cmd nopause`:
   - `git config --global core.longpaths true` when it can
   - `breakdown setup` (`.venv`, llvm-mingw, the pinned toolkit)
   - `APPLY_INPUT_PATCHES.cmd` and `APPLY_DISPLAY_PATCHES.cmd`
   - `breakdown doctor`
3. Runs `breakdown import` on the path you confirmed. Import checks the XBE title id (`0x4E4D0009`, Breakdown NA) and copies the disc into `game_files\` (`default.xbe`, `romdata\`, `stream\`, and the rest of the image). A `game_files\` that already holds a different dump is left alone until you run `breakdown import --replace` yourself.
4. Checks that `game_files\default.xbe` is an XBE with that title id, and that `romdata` and `stream` were extracted (names are matched without case).
5. Runs `breakdown doctor` again and prints `[PASS]` or `[FAIL]`.
6. If `build-win\breakdown_recomp.exe` already exists, `run_smoke.cmd` starts it for about eight seconds. `[PASS]` means the log contains `XBE loaded` or `Starting game`. The process is then closed. A fresh clone prints `[SKIP]` for this step.

`breakdown import` is the extractor. It uses the CLI's XDVDFS reader, the same format as `external\xboxrecomp\tools\xiso` after `breakdown setup`. Listing a disc by hand, from the repo root, once setup has finished:

```bat
py -3 -m tools.xiso ls "Breakdown (USA).iso"
```

Run that with `external\xboxrecomp` as the current directory, because `tools` is the toolkit package. Import remains the command that fills `game_files\`.

## Pass, fail, and skip

| Line | Meaning |
| --- | --- |
| `[PASS] import` | `game_files\` now holds this disc (or already matched it). |
| `[PASS] title id 0x4E4D0009` | `default.xbe` is Breakdown's title id from `game.toml`. |
| `[PASS] game_files\romdata` and `stream` | Those disc folders were extracted. |
| `[PASS] doctor` | The host has uv, the venv tools, the toolkit, llvm-mingw, and the XBE. |
| `[SKIP] native smoke` | The exe is not built yet, or this is not Windows. The import can still be good. |
| `[PASS] native smoke` | The built exe loaded the XBE. |
| `[FAIL]` | Read the line under it. A wrong game, a truncated extract, a missing toolchain, or an exe that exits before `XBE loaded` all land here. |

Logs from the smoke window: `run_smoke_stdout.txt` and `run_smoke_stderr.txt` (gitignored).

## What was already in the repo

- `SETUP.cmd` — long paths, `breakdown setup`, input and display patches, `breakdown doctor`, and a reminder to copy a dump into `game_files\`.
- `breakdown.cmd` / `breakdown.py` — `setup`, `doctor`, `all`, and `import` (title check, then extract into `game_files\`).
- `game.toml` — `[data] game_files = "game_files"`, `[xbe] path = "game_files/default.xbe"`, `title_id = 0x4E4D0009`.
- `run_native.cmd` — how to play a build you already made.
- `DISCLAIMER.md` and the README — bring your own dump. Nothing here distributes the game.
- After setup, `external\xboxrecomp\tools\xiso` (`ls`, `get`, `unpack`).

## What this flow adds

- `FIRST_RUN.cmd` — the wizard above.
- `first_run.py` — in-repo discovery, the prompt, the dump check, the short smoke, and the import/doctor orchestration.
- `run_smoke.cmd` — the launch check on its own, after a build exists.
- `SETUP.cmd nopause` so the wizard can continue, and a pointer to `FIRST_RUN.cmd` when `default.xbe` is missing.

## Steps that stay manual

- Own Breakdown and supply a dump you are allowed to make. No script downloads an ISO.
- Install Git for Windows, and use a short checkout path such as `D:\Emulation\Breakdown_Recomp`.
- Install Python 3.12+ from python.org with "Add python.exe to PATH" (`breakdown.py` itself runs on 3.9+).
- Install uv 0.5.31+ (`winget install --id=astral-sh.uv -e`). `breakdown setup` fetches the rest into this tree and checks the pinned hashes.
- The first `breakdown all` is a long local build. Until `build-win\breakdown_recomp.exe` exists, the native smoke stays a skip. Then run `run_native.cmd`.
- Re-run `APPLY_INPUT_PATCHES.cmd` and `APPLY_DISPLAY_PATCHES.cmd` if you delete `external\` (a later `FIRST_RUN.cmd` or `SETUP.cmd` does this too), then rebuild.
- To swap a dump that is already in `game_files\`, run `breakdown import --replace "path\to\image.iso"` yourself.
