# PC options inside the 4:3 frame

Date: 2026-11-06
Status: approved to implement (owner: keep working). Slice only.

## What this is

A host page titled **PC options**. It is an owned borderless overlay whose rectangle matches the 4:3 picture, so the letterbox bars stay outside it. It is not drawn into the frame, it is not a captioned desktop dialog, and it is not the Xbox Options screen. That screen is guest code from the disc dump, and the dump is not in this repo.

The present path that could draw a sprite into the swap chain lives in the toolkit (`external/xboxrecomp`, not in git). This slice does not patch that toolkit. Present also covers a child HWND, so the page is an owned borderless window (no caption, no taskbar button) parked on the 4:3 picture. Colors are original and procedural. A local pause screenshot is still required before anyone calls the palette final.

## What this is not

- Not guest Options integration.
- Not exclusive fullscreen, Hor+, VSync, presets, captions, remapping, or Anniversary content.
- Not a change to PR #6 or PR #7.

## Open and close

- Back held and Start rising, or Start held and Back rising, or both rising on the same poll, opens the page.
- Start alone does not open it. The game still receives that Start (title or pause).
- The opening poll does not forward Start, Back, the d-pad, A, or B.
- F10 toggles the page and does not send Start. Closing from F10 reverts an unsaved preview.
- Esc, B, or Back while Start is not held reverts and closes.
- Back while Start is still held does not close (that is the chord).
- While the page is open, Start, Back, the d-pad, A, and B are not forwarded. Analog sticks and triggers are cleared for that poll so the guest does not look or punch underneath.
- A rising direction moves once. A held direction repeats after 8 further polls, then every 4 polls.
- The same edge reported by the keyboard and by the pad poll moves one row, not two.

Keyboard: Backspace is Back, Enter is Start while the page is closed and A while it is open. Arrows are the d-pad. This matches `CONTROLS.txt`.

## Rows

Only settings main already applies:

| Row | Control |
|---|---|
| Internal resolution | `RECOMP_RENDER_SCALE` 1..4, next frame |
| Window size | `RECOMP_WINDOW_SCALE` 1..6, resize now |
| Aspect | letterbox `4:3`, `16:9`, `16:10`, `21:9` |
| Present filter | nearest, linear, integer |
| Window mode | windowed or borderless |
| Apply | write `breakdown_video.cfg`, stay open |
| Reset | preview stock (1x, 1x, 4:3, nearest, windowed) |
| Revert | restore the last applied settings and close |

A set environment variable locks that row. The value gains the words `launch script`. Reset does not change a locked row. Apply still writes the file; the variable keeps winning on the next launch, which the page already says.

Save failure leaves the file and the committed settings unchanged. The preview can stay on screen. Revert puts the window back on the committed settings. If the D3D window is missing, scale and filter still queue, and the status says size and borderless apply on the next launch.

## Placement

The 4:3 frame is centered in the client. A wider client leaves bars on the sides. The panel sits inside that frame with a margin. Tests fix the 1707×960 and 640×480 cases.

## Visual

No dumped art is in git. Paint is flat: navy panel, one steel highlight, amber value text, Segoe UI as the face already used by the old popup. Do not treat those colors as sampled from Breakdown. Polish waits on a local screenshot review. Ollama `tiqd-breakdown` was not reachable from this run (generation timed out, no tunnel).
