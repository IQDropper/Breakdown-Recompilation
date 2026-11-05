# Design: Mouse/Keyboard — auto-apply + Modern layout

Date: 2026-11-05  
Repo: IQDropper/Breakdown-Recompilation  
Track: 1 of 4 (M&K only; remapping UI / skins / platforms later)

## Goals
1. Fresh clone + `SETUP.cmd` ends with mouse/keyboard patches applied to the local toolkit.
2. Default **Modern** M&K layout tuned for Breakdown melee (punches on mouse buttons).
3. Document **Legacy** (current CONTROLS.txt Xbox-face-button mouse map) vs **Modern**.
4. Controllers keep working; F1 / focus-loss still releases mouse capture.

## Non-goals (this track)
- In-game remapping UI, Legacy/Modern scheme picker inside the Xbox menus
- Skin swaps, Linux/SteamOS/iOS/Android/Web ports

## Modern binding table (default)

| Input | Maps to Xbox | Role in Breakdown |
|-------|--------------|-------------------|
| WASD | Left stick | Move |
| Mouse move | Right stick | Look |
| LMB | **LT** | Left punch (combo input) |
| RMB | **RT** | Right punch (combo input) |
| Middle | Y | Face button Y |
| Mouse4 (back) | White | White |
| Mouse5 (fwd) | Black | Black |
| Scroll | D-pad U/D | Menus / misc |
| F1 | — | Release mouse capture |
| Z/X/C/V | A/B/X/Y | Face buttons (unchanged keys) |
| Q/E | White/Black | Shoulder analogs |
| 1/3 | LT/RT | Punch keys (backup) |
| Enter / Backspace | START / BACK | Pause / back |
| Arrows | D-pad | Menus |
| Shift / Ctrl | LS / RS click | Stick clicks |

**Legacy mouse buttons** (optional): `RECOMP_MOUSE_BUTTONS=X,B,Y,LT,RT` (old CONTROLS.txt).

Env (run_native.cmd):
```
set RECOMP_KEYBOARD=1
set RECOMP_MOUSE_SENS=1200
set RECOMP_MOUSE_INVERT=0
set RECOMP_MOUSE_BUTTONS=LT,RT,Y,WHITE,BLACK
rem Legacy mouse face-buttons: set RECOMP_MOUSE_BUTTONS=X,B,Y,LT,RT
```

## Auto-apply
After `breakdown setup` succeeds (toolkit under `external\xboxrecomp`):
1. `SETUP.cmd` runs `python patch_mouse.py` (or `py -3`) if `external\xboxrecomp\src\input\keyboard.c` exists.
2. Idempotent: safe to re-run.
3. New helper `APPLY_INPUT_PATCHES.cmd` for rebuilds after toolkit refresh.
4. README notes: re-run patches if you wipe `external\`.

## Files to change
- `SETUP.cmd` — call patch after setup
- `APPLY_INPUT_PATCHES.cmd` — new
- `run_native.cmd` — Modern mouse button defaults
- `CONTROLS.txt` — Modern + Legacy sections
- `README.md` — one-line pointer to CONTROLS / patches
- `docs/mk-controls-design.md` — this spec

## Verification
1. Fresh clone path has no `[mouse]` in toolkit until SETUP.
2. After SETUP + patches: `[mouse]` present in keyboard.c and d3d11 window proc.
3. Launch `run_native.cmd`, click window, look with mouse, LMB/RMB throw punches; pad still works.
4. F1 releases cursor.

## Risks
- Toolkit update overwrites patched files → re-run `APPLY_INPUT_PATCHES.cmd`.
- Wrong punch mapping if a dump uses different XAPI button semantics — verify in first mission melee.

## Acceptance
- [ ] SETUP.cmd auto-runs patch_mouse when toolkit present
- [ ] Modern defaults in run_native.cmd
- [ ] CONTROLS documents Modern vs Legacy
- [ ] No game assets committed