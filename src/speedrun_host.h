/* speedrun_host.h - the speedrun timer overlay (RECOMP_SPEEDRUN=1). Model: src/speedrun.c. */
#ifndef SPEEDRUN_HOST_H
#define SPEEDRUN_HOST_H

int speedrun_on(void);              /* RECOMP_SPEEDRUN=1 */
void speedrun_host_start(void);     /* file hook + overlay thread; speedrun_frame calls it the first time */
void speedrun_frame(int paused);    /* once per game frame, with the pause flag (0x29F6E0 == 1) */

#endif
