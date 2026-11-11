#ifndef TUNING_HOST_H
#define TUNING_HOST_H
/* The tuning range window (RECOMP_TUNING=1, F6) and the finalized values (tuning/tuning_final.json, every build).
 * See src/tuning_host.c and docs/DEV_TOOLS.md in the kit. */
#include <stdint.h>

void tuning_host_start(void);          /* display_host_init: load files; start the window when RECOMP_TUNING=1 */
int tuning_host_active(void);          /* the window is on, or a finalized value exists */
float tuning_host_move_speed(void);    /* "Move speed x" (1 = the game's own speed) */
void tuning_host_hero(uint32_t status);/* game thread, every hero update: status block or 0 */
#endif
