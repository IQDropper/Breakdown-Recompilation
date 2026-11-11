@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Applying D3D11 texture dump patch (idempotent)...

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
  echo [error] Python not on PATH; cannot run patch_tex_dump.py
  exit /b 1
)

echo --- patch_tex_dump.py ---
%PY% patch_tex_dump.py
if errorlevel 1 (
  echo [error] patch_tex_dump.py failed
  exit /b 1
)

echo [ok] RECOMP_TEX_DUMP texture dump applied. Rebuild the exe, then set
echo      RECOMP_TEX_DUMP=^<folder^> to write <hash16>_<w>x<h>_<fmt>.tga per unique texture.
exit /b 0
