/* play_record.h - P2.1 play recorder. RECOMP_PLAY_RECORD=1 writes a session folder while you play:
 * areas/sections entered, checkpoint save copies, screenshots (each area, every 5 minutes, the mark key),
 * the hero's position, slow frames, crashes. Pure parts here (src/play_record.c, tested by play_record_test.c);
 * the Windows side is src/play_record_host.c. */
#ifndef PLAY_RECORD_H
#define PLAY_RECORD_H

#include <stddef.h>
#include <stdint.h>

/* The area and section named by one of the game's own messages:
 *   "AREA_NAME_SET area/hon_under/hon_under_B_out, 1" -> "hon_under", "hon_under_B_out"
 *   "area_load area/hon_under/hon_under_A ..."        -> "hon_under", "hon_under_A" (map viewer)
 *   "AREA DATA SET  area/bettou"                       -> "bettou", ""
 * Returns 1 when the message names one. */
int pr_area_from_msg(const char *msg, char *area, size_t an, char *section, size_t sn);

/* The game's name for an area: "hon_under" -> "7F / Main Research Center"; unknown -> NULL. */
const char *pr_area_title(const char *area);

/* in as a JSON string body (no quotes): escapes " \ and control characters. Always terminates out. */
void pr_json_str(const char *in, char *out, size_t n);

/* "20261114-201502" */
void pr_session_name(int y, int mo, int d, int h, int mi, int s, char *out, size_t n);

/* Frame times between two reports. */
struct pr_frames {
    unsigned n, over50, over100;
    double sum_ms, worst_ms;
};
void pr_frames_add(struct pr_frames *f, double ms);
double pr_frames_fps(const struct pr_frames *f);   /* 0 when no frames */

#endif
