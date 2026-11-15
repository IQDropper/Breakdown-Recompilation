/* warmup.h - P6.3d shader warm-up: the game walks itself through the map viewer to fill the shader cache.
 *
 * RECOMP_WARMUP=1 (with RECOMP_DEBUG_MENU=1 and RECOMP_KEYBOARD=1; WARMUP_SHADERS.cmd sets all three) opens the
 * developers' F026 menu at boot, goes to MAKINO MENU -> MAP VIEWER, and loads map-list entries one after another:
 * each area is loaded, the camera turns and walks a little so its shaders get drawn (and compiled into the disk
 * cache), then F8 goes back to the list on the next entry. The buttons are pressed inside the game (the pad state
 * the game reads), not with real keys: the window does not need focus and nothing is typed into other programs.
 *
 * RECOMP_WARMUP_START = first map-list entry (default 0), RECOMP_WARMUP_COUNT = entries this run (default 20).
 * Long map-viewer runs are known to wear the game down, so the launcher runs it in batches and resumes after the
 * last "[WARMUP] loaded" line. The game exits by itself when its batch is done.
 *
 * warmup_step() is the pure state machine (tested by warmup_test.c); warmup_host.c wires it to the game. */
#ifndef WARMUP_H
#define WARMUP_H

#include <stdint.h>

enum warmup_state {
    WU_OFF, WU_BOOT, WU_MENU1, WU_MENU2, WU_LIST, WU_LOADING, WU_LOOK, WU_BACK, WU_DONE, WU_STUCK
};

struct warmup {
    enum warmup_state state;
    uint32_t since;        /* ms when the state began */
    int entry;             /* map-list entry being loaded */
    int left;              /* entries still to load this run */
    int loaded;            /* entries loaded this run */
    int area_seen;         /* an area-loaded message arrived in WU_LOADING */
};

/* What the game should get this frame. */
struct warmup_out {
    uint16_t buttons;      /* XINPUT d-pad / digital bits to OR in */
    uint8_t b_pressed;     /* B (analog button 1) held */
    int16_t look_x;        /* right stick X (camera turn) */
    int open_menu;         /* 1 once: open the developers' F026 menu */
    int leave_area;        /* 1 once: F8 in the area (back to the list, next entry) */
    int finished;          /* 1: batch done or stuck, exit the game */
};

#define WU_BOOT_MS     9000u    /* boot: the idle mode that accepts the menu key */
#define WU_MENU_MS     6000u    /* each menu screen */
#define WU_LIST_MS     3500u    /* map list shown, cursor placed */
#define WU_LOAD_MAX_MS 120000u  /* an area that never loads: stop (the launcher resumes after it) */
#define WU_SETTLE_MS   4000u    /* after the area message, before the camera moves */
#define WU_LOOK_MS     16000u   /* turn and walk */
#define WU_BACK_MS     4500u    /* the map viewer re-opens */

void warmup_init(struct warmup *w, int start, int count, uint32_t now);
void warmup_step(struct warmup *w, uint32_t now, struct warmup_out *o);
void warmup_area_loaded(struct warmup *w);
/* The map viewer opened map-list entry `map` (its own "in map" moment): the only trusted load signal. A different
 * entry than expected resyncs the cursor to it. */
void warmup_map_opened(struct warmup *w, int map);
const char *warmup_state_name(enum warmup_state s);

/* warmup_host.c (game side) */
int warmup_on(void);
void warmup_frame(void);                        /* once per frame */
void warmup_msg(const char *msg);               /* the game's debug messages */
void warmup_in_map(int map);                    /* the map viewer started entry `map` */
void warmup_pad(uint16_t *buttons, uint8_t analog[8], int16_t thumb[4]);   /* port 0 pad state */
int warmup_take_menu(void);
int warmup_take_f8(void);
int warmup_first_entry(void);                   /* map viewer cursor for the first list, -1 = leave it */

#endif
