@echo off
rem run_record.cmd - "Play + Record": play as usual (run_native.cmd) with the play recorder on (P2.1).
rem While you play it writes a session folder to %LOCALAPPDATA%\xboxrecomp\play_sessions\<date-time>\:
rem   events.jsonl  areas and sections entered, checkpoint saves, your position every 5 s, slow frames,
rem                 the frame rate each minute, marks, crashes
rem   shots\        a screenshot after each new section, one every 5 minutes, and one per mark
rem   saves\        copies of your save folders (at start, and each time the game saves). Your saves are only read.
rem Press F3 when something looks or sounds wrong: it takes a screenshot and notes where you are.
rem Quit normally. If the game crashes, the record keeps everything up to the crash.
set RECOMP_PLAY_RECORD=1
call "%~dp0run_native.cmd"
