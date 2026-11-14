#ifndef LOAD_TIMER_H
#define LOAD_TIMER_H
/* RECOMP_LOAD_LOG=1: [LOAD] lines per area/section load and the 5 s after it (load_timer.c). */
int load_timer_on(void);
void load_timer_msg(const char *msg);   /* each formatted game debug message */
void load_timer_frame(void);            /* each frame end */
#endif
