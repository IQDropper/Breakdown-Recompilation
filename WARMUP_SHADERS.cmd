@echo off
rem WARMUP_SHADERS.cmd - optional, once: fill the shader cache so first visits to areas do not stutter (P6.3d).
rem The game walks itself through every area (about an hour, window minimised, your saves untouched).
rem Stop with Ctrl+C any time; running it again continues where it stopped. Details: tools\warmup_shaders.ps1
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\warmup_shaders.ps1" %*
pause
