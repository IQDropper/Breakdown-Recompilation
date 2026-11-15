/* play_record_host.h - P2.1 play recorder, the Windows side (see play_record.h). */
#ifndef PLAY_RECORD_HOST_H
#define PLAY_RECORD_HOST_H

#include <stdint.h>

int play_record_on(void);                 /* RECOMP_PLAY_RECORD=1 */
void play_record_frame(void);             /* once per game frame (Render_SubmitFrame's DebugPrintf) */
void play_record_msg(const char *msg);    /* each of the game's own debug messages */
void play_record_hero(float x, float y, float z);   /* the hero's position, from the hero update */
void play_record_crash(uint64_t rip, uint64_t fault, uint32_t guest_va, int write);   /* the crash handler */

#endif
