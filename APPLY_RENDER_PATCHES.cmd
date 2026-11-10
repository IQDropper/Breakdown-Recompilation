@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Applying D3D11 render patches (idempotent)...

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
  echo [error] Python not on PATH; cannot run patch_poly_offset.py
  exit /b 1
)

echo --- patch_poly_offset.py ---
%PY% patch_poly_offset.py
if errorlevel 1 (
  echo [error] patch_poly_offset.py failed
  exit /b 1
)

echo [ok] D3D11 polygon offset applied. Rebuild the exe.
echo      RECOMP_POLY_OFFSET=0 turns it off for A/B.
exit /b 0
