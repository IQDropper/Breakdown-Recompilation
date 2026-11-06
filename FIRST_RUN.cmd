@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo === Breakdown first run ===
echo This script never downloads the game. Use a disc image you own.
echo.
rem Path resolution lives in first_run.py. The order, and why:
rem   1. build-win\Breakdown*.iso  same folder as the exe, inside this clone
rem   2. Breakdown*.iso here       one folder up from the exe, next to this script
rem   3. build-win\*.xiso
rem   4. *.xiso here
rem   5. build-win\*.xiso.iso
rem   6. *.xiso.iso here
rem   7. a path you paste          fallback when 1-6 find nothing
rem Those two folders are the whole search. A fresh clone does not need
rem an absolute path on some other drive.
echo Put the ISO or XISO next to the exe, or one folder up from it:
echo   %CD%\build-win
echo   %CD%
echo.
echo It looks there for Breakdown*.iso, then .xiso, then .xiso.iso.
echo Press Enter to accept the suggestion.
echo Pasting a path is the fallback when those folders have no image.
echo See DISCLAIMER.md and docs\first-run.md.
echo.

set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY (
  where python >nul 2>&1 && set "PY=python"
)
if not defined PY (
  echo [FAIL] Python 3.9+ is not on PATH.
  echo        Install Python 3.12+ from https://www.python.org/downloads/
  echo        Turn on "Add python.exe to PATH", then run FIRST_RUN.cmd again.
  pause
  exit /b 1
)

set "CHOICE=%TEMP%\breakdown-first-run-iso.txt"
if exist "%CHOICE%" del /q "%CHOICE%"

echo --- Where is your disc image? ---
%PY% first_run.py choose --save "%CHOICE%"
set "CHOOSE_ERR=%ERRORLEVEL%"
if not "%CHOOSE_ERR%"=="0" (
  echo.
  echo [FAIL] No disc image was selected.
  pause
  exit /b %CHOOSE_ERR%
)

echo.
echo --- Toolchain ^(long paths, breakdown setup, patches^) ---
call "%~dp0SETUP.cmd" nopause

echo.
echo --- Import, doctor, smoke ---
%PY% first_run.py wizard --path-file "%CHOICE%"
set "WIZ_ERR=%ERRORLEVEL%"
echo.
if "%WIZ_ERR%"=="0" (
  echo [PASS] First run finished. Read the lines above for doctor and smoke.
) else (
  echo [FAIL] First run hit an error. Read the lines above.
)
echo.
echo Still on you: Git, Python, uv, a disc you own, and the first
echo   breakdown all
echo Then play with run_native.cmd. Details: docs\first-run.md
echo.
pause
exit /b %WIZ_ERR%
