@echo off
rem run_native.cmd - play the recompiled Breakdown (native Windows exe, no emulator).
rem Each switch below is needed, and why (details: D:\Emulation\RE\pipeline\XBOXRECOMP_NOTES.md):
rem   RECOMP_FB_WINDOW=1  Windows shows no game window unless asked (item 15).
rem   RECOMP_VBLANK=1     deliver the 60 Hz vblank interrupt: D3D's frame pacing and our
rem                       D3D-callback fix (item 14) both run from it. Without it: frame 2, black.
rem Logs: run_stderr.txt (runtime), xbox_kernel.log (kernel calls).
cd /d %~dp0
rem RECOMP_PB_BACKEND=d3d11: draw on the graphics card (1.8x faster than the CPU rasteriser, 2026-11-03).
rem   No RECOMP_FB_WINDOW with it: that window shows guest framebuffer memory, which D3D11 never writes (blank).
set RECOMP_PB_BACKEND=d3d11
rem Occlusion (visibility) tests answered the same frame: deferred answers made lights/overlays flicker on and off.
set RECOMP_D3D11_OCC=sync
set RECOMP_VBLANK=1
rem RECOMP_KEYBOARD=1: keyboard also drives controller 1 (backup if the pad isn't seen). Controller: XAPI input bridge in src\recomp_manual.c
set RECOMP_KEYBOARD=1
rem Mouse: click the window to grab it, F1 releases. Controls + options: CONTROLS.txt
set RECOMP_MOUSE_SENS=1200
set RECOMP_MOUSE_INVERT=0
set RECOMP_MOUSE_BUTTONS=X,B,Y,LT,RT
build-win\breakdown_recomp.exe 1>run_stdout.txt 2>run_stderr.txt
