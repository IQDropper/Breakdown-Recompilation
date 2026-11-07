# Priority matrix

Rank a play issue with one line per tier.

- **Crash** = critical — process dies / hard hang
- **Unplayable** = high — cannot progress or control the game
- **Poor performance** = high — unusable framerate/stutter for play
- **Medium** — Functionality isn't working properly but the game is still playable.
- **Low** — Overall bugs or things that are purely cosmetic and don't impact play.

## Known issues

| Issue | Severity |
| --- | --- |
| Exclusive fullscreen acts as borderless (SetFullscreenState fails) | Medium. Borderless is still playable. |
| Shadow flicker on turn (no perf hit) | Medium |
| Mouse/keyboard gated on controller presence | Unplayable (high) for M&K-only (no pad) |
| PC options is an owned borderless host overlay over the 4:3 picture, not drawn into the frame | Medium until a play-test confirms it stays visible during Present, Back+Start, F10, and cfg reload. |
