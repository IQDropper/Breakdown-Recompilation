@echo off
rem run_native.cmd - play the recompiled Breakdown (native Windows exe, no emulator).
rem Each switch below is needed, and why (details: docs / XBOXRECOMP notes):
rem   RECOMP_FB_WINDOW=1  Windows shows no game window unless asked.
rem   RECOMP_VBLANK=1     deliver the 60 Hz vblank interrupt.
rem Logs: run_stderr.txt (runtime), xbox_kernel.log (kernel calls).
cd /d %~dp0
rem RECOMP_PB_BACKEND=d3d11: draw on the graphics card.
rem   No RECOMP_FB_WINDOW with it: that window shows guest framebuffer memory, which D3D11 never writes (blank).
set RECOMP_PB_BACKEND=d3d11
rem Occlusion (visibility) tests answered the same frame: deferred answers made lights/overlays flicker.
set RECOMP_D3D11_OCC=sync
set RECOMP_VBLANK=1
rem RECOMP_KEYBOARD=1: keyboard also drives controller 1. Controller: XAPI input bridge in src\recomp_manual.c
set RECOMP_KEYBOARD=1
rem Mouse: click window to grab, F1 releases. CONTROLS.txt = Modern vs Legacy.
rem Modern (default): LMB/RMB = left/right punch (LT/RT). Legacy face-buttons: X,B,Y,LT,RT
set RECOMP_MOUSE_SENS=1200
set RECOMP_MOUSE_INVERT=0
set RECOMP_MOUSE_BUTTONS=LT,RT,Y,WHITE,BLACK
build-win\breakdown_recomp.exe 1>run_stdout.txt 2>run_stderr.txt
