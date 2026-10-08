Breakdown Recompilation 0.2.2 for Windows
=========================================

A native PC build of Breakdown (Xbox, Namco, 2004), recompiled from your
own copy of the game. No game data is included: the first time you start
it, it asks for your Breakdown (USA) disc image and builds the game from
it, on your computer.

What you need
-------------
- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 11
- Your own Breakdown (USA) Xbox disc image (.iso)
- About 6 GB of free disk space for the one-time build
- An Xbox-style controller is recommended

Starting
--------
Keep the whole folder together (Breakdown.exe needs the tools folder
beside it). It runs from anywhere, but the one-time build is faster from
a local disk than from a network share.

1. Run Breakdown.exe.
   (Windows may warn that it is from an unknown publisher: choose
   "More info", then "Run anyway".)
2. First time only: choose your disc image and press Continue. The game
   is built from it. This takes a few minutes and happens once.
3. Press Play.

Later launches go straight to the menu. Updating to a new release
rebuilds the game once, the same way.

Options
-------
- Brightness: Original, in the middle, matches the original game. Move
  the slider left for a darker picture, right to lighten the dark parts.
- Full screen: fills the screen, with black bars at the sides on a
  widescreen monitor so the picture keeps its shape. F11 or Alt+Enter
  switches at any time, in the game as well.
- Saved games folder: where your saves are kept. Choosing a new folder
  does not move saves already made; copy them across yourself. "Open
  folder" shows the current one.

Known issues in 0.2.2
---------------------
- In some rooms the background music plays a short repeating fragment
  instead of the music. Turning the BGM slider down in the game's sound
  settings silences it.
- Loading after dying and choosing Retry can take a long time.
- Some textures appear a moment late.

Where things are kept
---------------------
%LOCALAPPDATA%\BreakdownRecomp\
    settings.ini   the disc image you chose, and your Options
    work\          the extracted game and the build (delete it to rebuild)
    logs\          launcher, build and game logs; please include the
                   newest ones with any bug report

Saved games are in %LOCALAPPDATA%\xboxrecomp\ unless you choose another
folder in Options (the default is where earlier builds kept them, so
existing saves carry over).

Close the game window to quit (Alt+F4 in full screen).

Controls in the menus: mouse, keyboard, or an Xbox-style controller
(D-pad or left stick to move, A to choose, B to go back).
