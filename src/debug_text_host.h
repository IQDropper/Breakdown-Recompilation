/* debug_text_host.h - the game's emptied DebugPrintf text, drawn by the host over the picture. */
#ifndef DEBUG_TEXT_HOST_H
#define DEBUG_TEXT_HOST_H

int debug_text_on(void);                 /* RECOMP_DEBUG_MENU=1 or RECOMP_DEBUG_TEXT=1 */
void debug_text_host_start(void);        /* once, at startup */
void debug_text_locate(int col, int row); /* the game's cursor (0x18F5C0), in character cells */
void debug_text_put(const char *s);      /* one formatted DebugPrintf */
void debug_text_color(int r, int g, int b); /* 0x18F570 */
void debug_text_frame_end(void);         /* Render_SubmitFrame: show what this frame printed */

#endif
