@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Applying analysis/translator patches (idempotent)...

if not exist "external\xboxrecomp\tools\recomp\disasm.py" (
  echo [skip] Toolkit not present yet: external\xboxrecomp\tools\recomp\disasm.py
  echo        Run "breakdown setup" first, then re-run this script.
  exit /b 0
)

set PY=
where py >nul 2>&1 && set PY=py -3
if not defined PY (
  where python >nul 2>&1 && set PY=python
)
if not defined PY (
  echo [error] Python not on PATH; cannot run patch_data_spots.py
  exit /b 1
)

echo --- patch_data_spots.py ---
%PY% patch_data_spots.py
if errorlevel 1 (
  echo [error] patch_data_spots.py failed
  exit /b 1
)

echo [ok] Translator skips the data ranges in config\data_spots.json.
echo      Regenerate and build: breakdown recomp, then breakdown build
exit /b 0
