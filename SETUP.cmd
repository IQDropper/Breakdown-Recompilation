@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo === Breakdown Recompilation - Windows setup helper ===
echo Repo: https://github.com/IQDropper/Breakdown-Recompilation
echo This script never downloads the game. Bring your own disc dump.
echo See DISCLAIMER.md and README.md.
echo.

git config --global core.longpaths true >nul 2>&1
if errorlevel 1 (
  echo [warn] Could not set core.longpaths. Run: git config --global core.longpaths true
) else (
  echo [ok] git core.longpaths enabled
)

echo.
echo Checkout path: %CD%
echo Tip: prefer a short path like D:\Emulation\Breakdown_Recomp
echo.

where py >nul 2>&1
if errorlevel 1 (
  where python >nul 2>&1
  if errorlevel 1 (
    echo [warn] Python not found on PATH.
    echo        Install Python 3.12+ from https://www.python.org/downloads/
    echo        Enable "Add python.exe to PATH", then re-run SETUP.cmd.
  )
)

if not exist "breakdown.cmd" (
  echo [error] breakdown.cmd missing. Run this from the repo root.
  exit /b 1
)

echo.
echo --- Running: breakdown setup (first time can take a while) ---
call breakdown.cmd setup
set SETUP_ERR=%ERRORLEVEL%
if not "%SETUP_ERR%"=="0" (
  echo.
  echo [warn] breakdown setup exited with code %SETUP_ERR%
  echo        Fix the error above, then re-run SETUP.cmd
)

echo.
echo --- Input patches (mouse + keyboard host bridge) ---
call APPLY_INPUT_PATCHES.cmd
set PATCH_ERR=%ERRORLEVEL%

echo.
echo --- Display patch (D3D11 window size for aspect / scale) ---
call APPLY_DISPLAY_PATCHES.cmd
set DISP_ERR=%ERRORLEVEL%

echo.
echo --- Running: breakdown doctor ---
call breakdown.cmd doctor
echo.

if not exist "game_files\default.xbe" (
  echo [next] Create game_files\ and copy YOUR dump here:
  echo          game_files\default.xbe
  echo          game_files\romdata\  and other disc files
  echo        Git ignores this folder. Do not commit it.
) else (
  echo [ok] Found game_files\default.xbe
)

echo.
echo --- Next commands ---
echo   breakdown all
echo   build-win\breakdown_recomp.exe
echo   run_native.cmd
echo.
echo Controls: CONTROLS.txt  (Modern mouse punches = LT/RT by default)
echo If you wipe external\, re-run APPLY_INPUT_PATCHES.cmd and APPLY_DISPLAY_PATCHES.cmd, then rebuild.
echo Display: docs\widescreen-hires.md  (run_native.cmd = 2x, 16:9 letterbox)
echo.
echo Read README.md and DISCLAIMER.md before sharing builds.
echo.
pause
endlocal