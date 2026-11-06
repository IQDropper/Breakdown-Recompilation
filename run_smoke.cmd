@echo off
rem Short launch check for an already-built build-win\breakdown_recomp.exe.
rem Skips with a clear line when the exe is not built yet. Does not import a disc.
setlocal EnableExtensions
cd /d "%~dp0"

set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY (
  where python >nul 2>&1 && set "PY=python"
)
if not defined PY (
  echo [FAIL] Python 3.9+ is not on PATH.
  exit /b 1
)

%PY% first_run.py smoke
set "SMOKE_ERR=%ERRORLEVEL%"
exit /b %SMOKE_ERR%
