@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Applying host input patches (idempotent)...

if not exist "external\xboxrecomp\src\input\keyboard.c" (
  echo [skip] Toolkit not present yet: external\xboxrecomp\src\input\keyboard.c
  echo        Run "breakdown setup" first, then re-run this script.
  exit /b 0
)

set PY=
where py >nul 2>&1 && set PY=py -3
if not defined PY (
  where python >nul 2>&1 && set PY=python
)
if not defined PY (
  echo [error] Python not on PATH; cannot run patch_mouse.py
  exit /b 1
)

echo --- patch_mouse.py ---
%PY% patch_mouse.py
if errorlevel 1 (
  echo [error] patch_mouse.py failed
  exit /b 1
)

echo --- patch_keyboard_remap.py ---
%PY% patch_keyboard_remap.py
if errorlevel 1 (
  echo [error] patch_keyboard_remap.py failed
  exit /b 1
)

echo [ok] Mouse/keyboard toolkit patches applied.
echo      Rebuild after this if the exe was already built: breakdown all
echo      Play with run_native.cmd  (see CONTROLS.txt)
exit /b 0