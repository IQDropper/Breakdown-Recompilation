# Toolchain pins

Versions this repo actually records. Rebuild with these so machines do not drift. Anything marked **not pinned — record what you used** has no in-repo pin; write down what you ran. Setup flow: [README.md](README.md). Hashes for the pinned downloads: `config/setup-pins.json`.

## Rebuild

| Piece | Pin | Where |
| --- | --- | --- |
| llvm-mingw (Windows target on every host) | `20260922` | `game.toml` `[toolchain]`, `config/setup-pins.json` |
| xboxrecomp | `0692070cb41d53d225711d8a8183efce0a388019` on `blinx2/portability` | `game.toml` `[toolkit]` |
| xboxrecomp-cli | `2ad369903e10363a999d1d78f01a8150b236a856` | `game.toml` `[cli]` |
| uv | `0.5.31` or newer | `breakdown.py` `UV_MIN` |
| Python | Floor `>=3.9` (`pyproject.toml` `requires-python`, `breakdown.py`). `SETUP.cmd` says install 3.12+ when `python` is missing. Not one patch. | those files |
| C | C11 | `CMakeLists.txt` |
| capstone | `5.0.9` | `pyproject.toml` (lifter; another version can change `gen/`) |
| pefile | `2024.8.26` | `pyproject.toml` |
| CMake | Minimum `3.20` (`cmake_minimum_required`). The `cmake` dependency is unpinned. No `uv.lock` in the tree. **not pinned — record what you used.** | `CMakeLists.txt`, `pyproject.toml` |
| Ninja | `ninja` is an unpinned dependency. No `uv.lock`. **not pinned — record what you used.** | `pyproject.toml` |
| MSVC | `CMakeLists.txt` adds `/bigobj` and `/LARGEADDRESSAWARE` only `if(MSVC)`. The Windows build pin is llvm-mingw. **not pinned — record what you used.** | `CMakeLists.txt`, `game.toml` |
| Windows SDK | Link line is `d3d11`, `dxgi`, `dxguid`, `xinput`, `winmm`, `dbghelp`. No SDK version. **not pinned — record what you used.** | `CMakeLists.txt` |
| vcpkg | Not used. | — |
| CI image | No `.github/workflows`. **not pinned — record what you used.** | — |

`migrate_to_onedrive.ps1` recorded a one-off install of `cmake==4.4.4`, `ninja==1.13.2`, and a uv venv on Python `3.14`. `breakdown setup` follows `pyproject.toml`, which leaves CMake and Ninja unpinned. `overnight.ps1` puts Python 3.12's Scripts dir on `PATH` for uv.

`first_run.py` / `tests/test_first_run.py` ([PR #10](https://github.com/IQDropper/Breakdown-Recompilation/pull/10)) use the standard library on the same Python floor (`python -m unittest`). No extra test-package pin. `pytest` and `ruff` are optional dev dependencies with no versions: **not pinned — record what you used.**

## Packaging (not required to rebuild)

NSIS `3.13`, sha256-pinned in `config/setup-pins.json`. Packaging in `game.toml` is opt-in.

## Optional review toolchain

TIQD's local Ollama model is `tiqd-breakdown`. Rebuilding the game does not use Ollama. This repo has no Modelfile and no model digest. **not pinned — record what you used.**
