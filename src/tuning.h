/*
 * tuning.h - the tuning range's model (no Windows, unit-tested in src/tuning_test.c).
 *
 * Rows of live values with min / max / ideal, the game's default, and the finalized (shipping) value. Files:
 *   tuning_ranges.json  {"key": {"min": a, "max": b, "ideal": c}, ...}   (committed)
 *   tuning_final.json   {"key": value, ...}                              (committed; loaded in every build)
 *   presets             same format as final, in user data
 * Finalize writes the final file, copies the old one into history/, and appends one changelog line per changed value.
 * The window is src/tuning_host.c; the live reads and writes are in src/recomp_manual.c. See docs/DEV_TOOLS.md (kit).
 */
#ifndef TUNING_H
#define TUNING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TUNE_TAB_PLAYER = 0, TUNE_TAB_SNAPSHOT, TUNE_TAB_FINALIZED, TUNE_TAB_COUNT };
enum { TUNE_GREEN = 0, TUNE_YELLOW, TUNE_RED, TUNE_GREY };

struct tune_row {
    const char *key;      /* stable id in the files, e.g. "player.max_life" */
    const char *name;     /* shown in the window */
    int tab;
    float step;
    int state;            /* 1 = live state (life now), never finalized */
    /* runtime */
    int have_cur, have_def, have_range, have_ideal, have_fin;
    float cur, def, min, max, ideal, fin;
};

void tuning_reset(void);
int tuning_count(void);
struct tune_row *tuning_row(int i);
int tuning_find(const char *key);

/* The game's value as first seen this run (Default) and as now (Current). Blank min/max become x0.25..x4 of the
 * default (or 0..4 when the default is 0). */
void tuning_observe(int i, float value);
/* Set Current (clamped to min..max); returns the value used. */
float tuning_set(int i, float value);
/* Step by n steps (sign = direction). */
float tuning_nudge(int i, int steps);
int tuning_colour(int i);
/* Percent difference of Current vs Finalized, or 0 with *have = 0 when there is no finalized value. */
float tuning_delta_pct(int i, int *have);

/* Files. Return 1 on success, 0 when the file is missing (load), -1 on error. */
int tuning_load_ranges(const char *path);
int tuning_save_ranges(const char *path);
int tuning_load_final(const char *path);
int tuning_save_values(const char *path);   /* a preset: Current of every non-state row */
int tuning_load_preset(const char *path);   /* sets Current from the file */

/* Finalize: dir holds tuning_final.json and history/; changelog is appended. date like "2026-11-11 22:10".
 * Writes every non-state row whose Current differs from Default. Returns the number of values written, -1 on error. */
int tuning_finalize(const char *dir, const char *changelog, const char *date, const char *note);
/* Markdown table of every row (Snapshot export). */
int tuning_snapshot(const char *path, const char *date);

#ifdef __cplusplus
}
#endif
#endif
