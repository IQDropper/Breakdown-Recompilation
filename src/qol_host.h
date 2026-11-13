/* qol_host.h - S5.3 quality of life: FPS overlay (RECOMP_FPS_OVERLAY=1, F5), F12 screenshots, intro skip switch.
 * Pure parts: src/qol.c. */
#ifndef QOL_HOST_H
#define QOL_HOST_H

int qol_fps_overlay_on(void);   /* RECOMP_FPS_OVERLAY=1 */
int qol_screenshots_on(void);   /* on unless RECOMP_SCREENSHOT=0 */
int qol_skip_intro_on(void);    /* RECOMP_SKIP_INTRO=1 (the skip itself is in src/recomp_manual.c) */
void qol_frame(void);           /* once per game frame (Render_SubmitFrame's DebugPrintf, from 0x18EA4E) */
void qol_note(const char *text);   /* a short line in the overlay for about 2 seconds */

#endif
