@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Base renderer and loader fixes every install needs. Foster's build has had them since early November, but
rem SETUP.cmd never ran them, so a fresh install could draw black rooms (depth pre-pass), lose vertices or break
rem render-to-texture. Runs first: the input, display and render patches build on top of these. All idempotent.
echo Applying base fixes (idempotent)...

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
  echo [error] Python not on PATH
  exit /b 1
)

for %%F in (
  patch_alloc_pages.py
  patch_section_tail.py
  patch_sema_dma.py
  patch_d3d_callback.py
  patch_fb_window.py
  patch_trace_regs.py
  patch_inline_vsize.py
  patch_zonly.py
  patch_vs_strict.py
  patch_rtt_sub.py
  patch_skip_why.py
  patch_skip_why2.py
  patch_px_trigger.py
) do (
  echo --- %%F ---
  %PY% %%F
  if errorlevel 1 (
    echo [error] %%F failed
    exit /b 1
  )
)

echo [ok] Base fixes: page-rounded memory, XBE section tails, GPU semaphores, D3D callbacks, window close, inline vertex sizes,
echo      depth-only passes, exact vertex maths (no black rooms), render-to-texture, debug counters. Rebuild the exe.
exit /b 0
