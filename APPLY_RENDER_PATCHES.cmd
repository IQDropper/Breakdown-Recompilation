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

echo --- patch_d3d11_prof.py ---
%PY% patch_d3d11_prof.py
if errorlevel 1 (
  echo [error] patch_d3d11_prof.py failed
  exit /b 1
)

echo [ok] D3D11 draw-path timing applied ([D3D11-PROF] lines with RECOMP_FLIP_LOG).
echo --- patch_d3d11_omcache.py ---
%PY% patch_d3d11_omcache.py
if errorlevel 1 (
  echo [error] patch_d3d11_omcache.py failed
  exit /b 1
)

echo [ok] D3D11 output-merger bind cache (RECOMP_D3D11_OM_CACHE=0 turns it off).
echo --- patch_d3d11_rtpark.py ---
%PY% patch_d3d11_rtpark.py
if errorlevel 1 (
  echo [error] patch_d3d11_rtpark.py failed
  exit /b 1
)

echo [ok] D3D11 retired render targets reused (RECOMP_D3D11_RT_PARK=0 re-creates them).
echo --- patch_pb_pipeline.py ---
%PY% patch_pb_pipeline.py
if errorlevel 1 (
  echo [error] patch_pb_pipeline.py failed
  exit /b 1
)

echo [ok] Opt-in pushbuffer pipelining (RECOMP_PB_PIPELINE=1).
echo --- patch_d3d11_vsync.py ---
%PY% patch_d3d11_vsync.py
if errorlevel 1 (
  echo [error] patch_d3d11_vsync.py failed
  exit /b 1
)

echo [ok] VSync can be switched while running (PC settings page).
echo --- patch_d3d11_stock_size.py ---
%PY% patch_d3d11_stock_size.py
if errorlevel 1 (
  echo [error] patch_d3d11_stock_size.py failed
  exit /b 1
)

echo [ok] Render scale 1 fills the window (no top-left quarter).
echo [ok] D3D11 polygon offset applied. Rebuild the exe.
echo      RECOMP_POLY_OFFSET=0 turns it off for A/B.
exit /b 0
