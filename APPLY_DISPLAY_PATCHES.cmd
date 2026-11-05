@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Applying D3D11 window-size patch (idempotent)...

if not exist "external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c" (
  echo [skip] Toolkit not present yet: external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c
  echo        Run "breakdown setup" first, then re-run this script.
  exit /b 0
)

set PY=
where py >nul 2>&1 && set PY=py -3
if not defined PY (
  where python >nul 2>&1 && set PY=python
)
if not defined PY (
  echo [error] Python not on PATH; cannot run patch_d3d11_window.py
  exit /b 1
)

echo --- patch_d3d11_window.py ---
%PY% patch_d3d11_window.py
if errorlevel 1 (
  echo [error] patch_d3d11_window.py failed
  exit /b 1
)

echo [ok] D3D11 window hook applied.
echo      If this upgraded an older patch, rebuild the toolkit and the exe.
echo      A build-win configured before display_host.c drops CMakeCache.txt
echo      so the next breakdown build picks that file up. Then: breakdown all
echo      Play with run_native.cmd  (see docs\widescreen-hires.md)
exit /b 0
