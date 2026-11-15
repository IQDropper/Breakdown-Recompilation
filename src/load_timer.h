#ifndef LOAD_TIMER_H
#define LOAD_TIMER_H
/* RECOMP_LOAD_LOG=1: [LOAD] lines per area/section load and the 5 s after it (load_timer.c). */
int load_timer_on(void);
void load_timer_msg(const char *msg);   /* each formatted game debug message */
void load_timer_frame(void);            /* each frame end */
/* P6.2: the data manager's setup parts, timed around the game's own functions (recomp_manual.c). */
enum { LT_TEX, LT_SOUND, LT_MODEL, LT_SCRIPT, LT_MAPIDS, LT_PARTS };
double load_timer_now(void);
void load_timer_part(int part, double ms);
#endif
