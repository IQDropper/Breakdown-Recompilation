# PC options implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the captioned F10 popup with a host PC options page inside the 4:3 frame, covering the five settings main already applies.

**Architecture:** `src/pc_options.c` is single-threaded and holds the page, the chord, the filter, and the layout math. `src/pc_options_host.c` is an owned borderless window parked on the 4:3 picture (Present covers a child HWND) plus the existing resize/rollback path. `video_menu_start` only starts that host. The input bridge calls the model under a lock and queues resize on the game window.

**Tech Stack:** C11, Win32 child HWND, existing `video_config` / `display_host`. gcc and mingw `-Wall -Wextra -Werror` for the host units. No dump.

## Global Constraints

- Branch from `origin/main` (`5bc03da`). Do not merge or rebase PR #6 or PR #7.
- No exclusive, Hor+, VSync, preset, or caption row.
- No guest Options claim. No dumped art. No `game_files`, `external`, `build-win`, or `gen` commits.
- Environment variables still win over `breakdown_video.cfg`.
- Start alone still reaches the guest.

---

### Task 1: Page model

**Files:**
- Create: `src/pc_options.h`, `src/pc_options.c`, `src/pc_options_test.c`
- Test: `gcc -std=c11 -Wall -Wextra -Werror -I src -o pc_options_test src/pc_options_test.c src/pc_options.c src/video_config.c src/display_host.c`

**Interfaces:**
- Produces: `pc_options_set_baseline`, `pc_options_set_locks`, `pc_options_on_port`, `pc_options_set_ui_bit`, `pc_options_on_f10`, `pc_options_on_esc`, `pc_options_frame_rect`, `pc_options_panel_rect`, `pc_options_save_result`, `PC_ACT_NONE/OPENED/PREVIEW/SAVE/REVERT`, `PC_LOCK_*`, `PC_PAD_*`.

- [x] **Step 1:** Write `src/pc_options_test.c` against that API. Run it and confirm the link or assertions fail.
- [x] **Step 2:** Implement `src/pc_options.c` until the test prints `all passed`.
- [x] **Step 3:** Commit the model, the test, the spec, and this plan.

### Task 2: Host child window and input

**Files:**
- Create: `src/pc_options_host.c`, `src/pc_options_host.h`
- Modify: `src/video_menu.c` (start the host; delete the captioned popup), `src/recomp_manual.c`, `patch_input_bridge.py`, `CMakeLists.txt`
- Modify docs: `docs/video-options.md`, `CONTROLS.txt`, `README.md`, `ROADMAP.md`, `PRIORITY_MATRIX.md`

**Interfaces:**
- Consumes: Task 1.
- Produces: `pc_options_host_start`, `pc_options_host_submit_pad`.

- [x] **Step 1:** Window class `BreakdownPcOptions`, an owned borderless popup placed with `pc_options_panel_rect` in screen coordinates. F10, Esc, arrows, Enter, and Backspace call the model. Revert calls the existing borderless/windowed resize with the committed settings.
- [x] **Step 2:** `sub_00205439` submits the pad word and writes back filtered buttons. While the result says so, zero analogs and thumbs.
- [x] **Step 3:** `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror` compiles `pc_options.c`, `pc_options_host.c`, `video_menu.c`, `video_config.c`, and `display_host.c`. Re-run `pc_options_test`, `video_config_test`, and `display_host` with `-DDISPLAY_HOST_TEST`.
- [x] **Step 4:** Commit, push `cursor/pc-options-inframe-eb38`, open a PR onto `main`.

## Blocker recorded in the spec

A swap-chain sprite needs the toolkit present path. This plan does not pretend the child window is that sprite.
