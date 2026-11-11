/* speedrun.h - the speedrun timer's model: in-game time, loads, pause and splits. No Windows, no game memory:
 * the host (src/speedrun_host.c) feeds it times in milliseconds and the game's events, and draws what it says. */
#ifndef SPEEDRUN_H
#define SPEEDRUN_H

#include <stddef.h>
#include <stdint.h>

#define SPEEDRUN_MAX_SPLITS 64
#define SPEEDRUN_NAME_LEN   48

/* A load lasts until no area file has been opened for this long ... */
#define SPEEDRUN_LOAD_QUIET_MS 500u
/* ... but never longer than this, so a file the game keeps re-opening cannot freeze the timer. */
#define SPEEDRUN_LOAD_MAX_MS   30000u

enum speedrun_state {
    SPEEDRUN_READY,     /* reset; the first area load starts the run */
    SPEEDRUN_RUNNING,
};

struct speedrun_split {
    char area[SPEEDRUN_NAME_LEN];   /* e.g. "hon_under_A" */
    uint64_t igt_ms;                /* in-game time when this area started loading */
    uint64_t rta_ms;                /* real time since the run started */
    uint64_t load_ms;               /* total load time so far */
};

struct speedrun {
    enum speedrun_state state;
    uint64_t start_t;      /* clock at run start */
    uint64_t last_t;       /* clock at the last update */
    uint64_t igt_ms;       /* counted time (not loading, not paused) */
    uint64_t load_ms;      /* time spent loading during the run */
    uint64_t pause_ms;     /* time spent paused (not loading) during the run */
    int paused;
    int loading;
    uint64_t load_start_t, load_last_t;
    unsigned quiet_ms, max_ms;
    int nsplits;
    struct speedrun_split splits[SPEEDRUN_MAX_SPLITS];
};

/* What one event did, for the host (it writes splits.csv). */
enum speedrun_event {
    SPEEDRUN_EV_NONE,
    SPEEDRUN_EV_START,   /* the run started; splits[0] is the first area */
    SPEEDRUN_EV_SPLIT,   /* a new split was added at splits[nsplits - 1] */
};

void speedrun_init(struct speedrun *s);   /* READY, default load timings */
void speedrun_reset(struct speedrun *s);  /* back to READY, keeps the load timings */
/* Move the clock to t (ms, never going back). Safe to call any time. */
void speedrun_update(struct speedrun *s, uint64_t t);
/* The game's pause flag (0x29F6E0: 0 = playing, 1 = paused) at time t. */
void speedrun_set_paused(struct speedrun *s, int paused, uint64_t t);
/* A guest file path the game opened at time t. */
enum speedrun_event speedrun_on_path(struct speedrun *s, const char *path, uint64_t t);

uint64_t speedrun_igt(struct speedrun *s, uint64_t t);   /* updates, then returns IGT */
uint64_t speedrun_rta(const struct speedrun *s, uint64_t t);
int speedrun_loading(const struct speedrun *s, uint64_t t);

/* "area\<name>\<name>_<L>.pak" (any case, any device prefix) -> "<name>_<L>" in out; 1 when it matched. */
int speedrun_area_pak(const char *path, char *out, size_t n);
/* 1 when the path is under an "area\" folder (it keeps a load going). */
int speedrun_area_path(const char *path);

/* ms -> "M:SS.cc" or "H:MM:SS.cc" (centiseconds, rounded down). */
void speedrun_format(uint64_t ms, char *out, size_t n);
/* One splits.csv row for split i (no newline). run_id names the run (the host uses its start date). */
void speedrun_csv_row(const struct speedrun *s, int i, const char *run_id, char *out, size_t n);
/* The row for the end of the run (Ctrl+F11 reset or exit): split = nsplits, area = "end", times at t. */
void speedrun_csv_end(struct speedrun *s, uint64_t t, const char *run_id, char *out, size_t n);
#define SPEEDRUN_CSV_HEADER "run,split,area,igt_ms,igt,segment_ms,rta_ms,load_ms"

#endif
