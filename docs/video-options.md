# Video options

Date: 2026-11-05

Players change performance from inside the running game. The knobs are the same `RECOMP_*` variables `run_native.cmd` already sets. This note is the menu and the file. Aspect and scale behaviour is [widescreen-hires.md](widescreen-hires.md).

## Why a host window

The Xbox Options screen is guest code. That code is generated from a disc dump, and the dump is not in this repo, so there is no stable place to add a row to that screen. PC options is an owned borderless host overlay positioned over the 4:3 picture. It is not drawn into the frame, and it is not that Xbox screen.

The toolkit present path is not in this repo. Present covers a child window, so this is not a child of the swap chain. The overlay has no caption and no taskbar button. Its rectangle comes from the 4:3 picture inside the client, in this process's DPI space. The letterbox bars stay outside that rectangle. Colors are procedural (navy panel, steel highlight, amber values). They are not sampled from the game. A local pause screenshot is still the review for visual polish.

## Open it

1. Launch the game (`run_native.cmd` or the exe).
2. Click the game window if you want the picture to be the one you are tuning.
3. Hold **Back** and press **Start** (Backspace and Enter, or the controller buttons). Start alone still pauses and still advances the title. The opening press is not forwarded.
4. Or press **F10**. F10 toggles the page and does not send Start.

While the page is open, Start, Back, the d-pad, A, and B stay on the page. Sticks and triggers are cleared for those polls. Up and Down move. Left and Right preview. A or Enter on Apply writes the file and leaves the page open. Reset previews stock (1x, 1x, 4:3, nearest, windowed) except a row a launch script owns. Esc, B, or Revert puts the picture back and closes. A held direction repeats. A row marked `launch script` cannot be changed; that variable still wins over the file.

stderr: `[PC] owned borderless host overlay over the 4:3 picture (not drawn into the frame, not the Xbox Options screen)`.

The rows are:

| Row | Saved as | Preview |
|---|---|---|
| Internal resolution | `RECOMP_RENDER_SCALE` 1..4 | Next frame. Higher costs more. 1x is the Xbox internal size. |
| Window size | `RECOMP_WINDOW_SCALE` 1..6 | Resizes the window now. Height is 480 times this. |
| Aspect | `RECOMP_DISPLAY_ASPECT` `4:3`, `16:9`, `16:10`, `21:9` | Resizes now. Letterbox only. Hor+ is not available. |
| Present filter | `RECOMP_PRESENT_FILTER` `nearest`, `linear`, `integer` | Next frame. `integer` is the present path’s integer fit: the largest whole multiple of the frame that fits in the window, letterboxed. It does not stretch to fill. See [widescreen-hires.md](widescreen-hires.md). |
| Window mode | `RECOMP_PRESENT_FULLSCREEN` `0`, `1`, or `exclusive` (`2` is read too) | Windowed, borderless on the monitor, or exclusive. Left and Right cycle the three. Exclusive needs `APPLY_DISPLAY_PATCHES.cmd` and a toolkit rebuild; without it the game logs a line and uses borderless. The cfg line is `0`, `1`, or `exclusive`, so a file from before this option still loads. |
| Hold to repeat | `RECOMP_A11Y_REPEAT` `off`, `a`, `b`, `x`, `y`, `abxy` | At once. Accessibility (A11Y-2): while that button (or a key mapped to it) is held, the game sees it pressed and released 10 times a second, so mashing prompts need only a hold. Off by default. |
| Hold toggle | `RECOMP_A11Y_TOGGLE` `off`, `rt`, `lt`, `both` | At once. Accessibility (A11Y-3): one press of that trigger holds it, the next press lets go. Off by default. |

The two hold rows work in the input bridge (`src/recomp_manual.c`, XInputGetState) on the pad's analog buttons, before the game builds its pad blocks (0x2988F0, 0x29BEB0). The logic is `src/a11y_input.c` (test: `src/a11y_input_test.c`). A file without these lines loads as off. stderr: `[A11Y] hold to repeat: ..., hold toggle: ...`.

Exclusive is DXGI `SetFullscreenState` on the desktop mode of the monitor the window is on. The 4:3 frame stays letterboxed in that client (bars on a wide monitor); window size and aspect only matter when you leave exclusive, and the window comes back at the size it had before. It is entered on the first frame after the window has focus, retried every 30 frames (20 times) if Windows says it is not available yet and again when the window gains focus, left on Alt+Tab (DXGI minimises the window) and entered again when you come back, and left when the window is closed so the desktop mode is restored. Alt+Enter does nothing. The PC options page is a separate owned window; whether it shows over an exclusive swap chain is checked in game (see docs/widescreen-hires.md for the coordinator smoke).

A wide aspect at internal 1x with nearest still pins the image to the top left. That is the present path, not a menu bug. The filter row says so. Linear or integer centres it. The same rule as a launch with those variables.

VSync, a frame cap, stretch-to-fill, hor+, and the flicker switches (`RECOMP_D3D11_OCC`, `RECOMP_D3D11_NO_RTT`, mip, point) are not in this menu.

## Config file

Apply and OK write **`breakdown_video.cfg` beside `breakdown_recomp.exe`**.

A `run_native.cmd` launch runs `build-win\breakdown_recomp.exe`, so the file is `build-win\breakdown_video.cfg`, not the repo root. The startup log prints that full path. `sources` is `env` when that variable is set, `file` when only the config has it, and `default` when neither does. `run_native.cmd` before any save looks like:

```text
[VIDEO] config D:\...\build-win\breakdown_video.cfg (not created yet)
[VIDEO] sources render=env window=env aspect=env filter=env fullscreen=default
```

A later launch of the exe with those variables unset looks like:

```text
[VIDEO] config D:\...\build-win\breakdown_video.cfg (loaded)
[VIDEO] sources render=file window=file aspect=file filter=file fullscreen=file
```

On launch, for each variable:

- Set and non-empty: the environment wins. `run_native.cmd` keeps working.
- Unset or empty: the file is used.
- Both missing: stock. Internal 1x, window 1x, 4:3, nearest, windowed (640×480).

The menu does not write the file until Apply or OK.

## Recovery

A bad `breakdown_video.cfg`, or a launch that keeps coming back at a size the machine cannot show, is cleared by deleting that file beside the exe. If any of `RECOMP_RENDER_SCALE`, `RECOMP_WINDOW_SCALE`, `RECOMP_DISPLAY_ASPECT`, `RECOMP_PRESENT_FILTER`, or `RECOMP_PRESENT_FULLSCREEN` is set, clear those too: a set variable overrides the file, including after the file is gone. With the file gone and those variables unset, the next launch is stock: internal 1x, window 1x, 4:3, nearest, windowed (640×480).

## What to click

Full `breakdown_recomp.exe` link and this click-through were not run for the merge `2b8408a`. A stub link of the host objects is not that validation. The overlay is not drawn into the frame.

1. Start `build-win\breakdown_recomp.exe` with no `RECOMP_RENDER_SCALE`, `RECOMP_WINDOW_SCALE`, `RECOMP_DISPLAY_ASPECT`, `RECOMP_PRESENT_FILTER`, or `RECOMP_PRESENT_FULLSCREEN`, and with no cfg beside the exe. The log should say the config is not created yet, and every source is `default`. The window is 640×480.
2. Hold Back and press Start, or press F10. The overlay opens over the 4:3 picture and stays above the game window while the game presents. stderr: `[PC] Back+Start opens the host overlay over the 4:3 picture`. Set internal resolution to 2x, window size to 2x, aspect to 16:9, filter to nearest, window mode to windowed. The window should grow as you change size and aspect. stderr: `[PC] apply ...`. Esc reverts that preview. Press Start alone on the title screen and confirm the page stays closed. Press F10 and confirm the same page toggles.
3. Apply. `build-win\breakdown_video.cfg` should appear. stderr: `[PC] saved ...`. Esc or Revert after an unsaved size change restores the previous window. A failed save leaves the previous file in place.
4. Quit and launch the exe again the same way (no variables). The log should say the config loaded and the sources are `file`. The window opens at the 16:9 size from the file (1707×960 at window scale 2).
5. `run_native.cmd` still forces its own scale, window, aspect, and filter. Those sources log as `env`. The file does not change that launch. Comment out one `set` in the script if you want the file to win for that one variable.
