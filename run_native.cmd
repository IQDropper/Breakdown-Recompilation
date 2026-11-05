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
rem The exe defaults this too (src\env\recomp_env_game.h). A late query still falls back to visible after 250 ms:
rem docs\flicker-investigation.md. A/B deferred: run_native_occdefer.cmd
set RECOMP_D3D11_OCC=sync
set RECOMP_VBLANK=1
rem RECOMP_KEYBOARD=1: keyboard also drives controller 1. Controller: XAPI input bridge in src\recomp_manual.c
set RECOMP_KEYBOARD=1
rem Mouse: click window to grab, F1 releases. CONTROLS.txt = Modern vs Legacy.
rem Modern (default): LMB/RMB = left/right punch (LT/RT). Legacy face-buttons: X,B,Y,LT,RT
set RECOMP_MOUSE_SENS=1200
set RECOMP_MOUSE_INVERT=0
set RECOMP_MOUSE_BUTTONS=LT,RT,Y,WHITE,BLACK
rem Display (docs\widescreen-hires.md, docs\video-options.md). Frame stays 4:3.
rem Hor+ is not in this toolkit pin (0692070). Exclusive is a display mode, not a wider camera.
rem Needs APPLY_DISPLAY_PATCHES.cmd and a rebuild or the window stays 640x480.
rem These variables override breakdown_video.cfg beside the exe. In-game Video is F10.
rem Unset a line here to use the saved value for that one setting.
rem Internal resolution 1..4 (1 = Xbox pixels). Window height is 480*WINDOW_SCALE.
set RECOMP_RENDER_SCALE=2
set RECOMP_WINDOW_SCALE=2
rem 4:3 | 16:9 | 16:10 | 21:9. Wider values pillarbox. They do not widen the camera.
set RECOMP_DISPLAY_ASPECT=16:9
rem nearest | linear | integer. Scale 2 already letterboxes a wide window.
set RECOMP_PRESENT_FILTER=nearest
rem Windowed if unset. 1 = borderless. exclusive (or 2) = mode switch at this window size.
rem set RECOMP_PRESENT_FULLSCREEN=1
build-win\breakdown_recomp.exe 1>run_stdout.txt 2>run_stderr.txt
