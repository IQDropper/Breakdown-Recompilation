/* play_record.c - P2.1 play recorder, the parts with no Windows calls (see play_record.h). */
#include "play_record.h"

#include <stdio.h>
#include <string.h>

static void copy_until(const char *p, const char *stop, char *out, size_t n)
{
    size_t k = 0;
    if (!n)
        return;
    while (*p && !strchr(stop, *p) && k + 1 < n)
        out[k++] = *p++;
    out[k] = 0;
}

int pr_area_from_msg(const char *msg, char *area, size_t an, char *section, size_t sn)
{
    static const char *const keys[] = { "AREA_NAME_SET area/", "area_load area/", "AREA DATA SET  area/" };
    const char *p = NULL;
    size_t i;
    if (an) area[0] = 0;
    if (sn) section[0] = 0;
    for (i = 0; i < sizeof keys / sizeof keys[0] && !p; i++) {
        p = strstr(msg, keys[i]);
        if (p)
            p += strlen(keys[i]);
    }
    if (!p || !*p || *p == ' ' || *p == ',')
        return 0;
    copy_until(p, "/, \r\n", area, an);
    p += strcspn(p, "/, \r\n");
    if (*p == '/')
        copy_until(p + 1, ", \r\n", section, sn);
    return an && area[0] != 0;
}

const char *pr_area_title(const char *area)
{
    /* Foster's names for the areas he knows (memory: area-names); others stay as the game's folder name. */
    static const struct { const char *id, *title; } t[] = {
        { "hon_under", "7F / Main Research Center" },
        { "bettou", "Annex" },
        { "hon_hon", "Main Hospital" },
        { "gesui", "Sewers" },
        { "kyodo", "Utility Tunnel" },
    };
    size_t i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (strcmp(area, t[i].id) == 0)
            return t[i].title;
    return NULL;
}

void pr_json_str(const char *in, char *out, size_t n)
{
    size_t k = 0;
    if (!n)
        return;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        char tmp[8];
        const char *add = tmp;
        if (c == '"') add = "\\\"";
        else if (c == '\\') add = "\\\\";
        else if (c == '\n') add = "\\n";
        else if (c == '\r') add = "\\r";
        else if (c == '\t') add = "\\t";
        else if (c < 0x20) snprintf(tmp, sizeof tmp, "\\u%04x", c);
        else { tmp[0] = (char)c; tmp[1] = 0; }
        if (k + strlen(add) + 1 > n)
            break;
        memcpy(out + k, add, strlen(add));
        k += strlen(add);
    }
    out[k] = 0;
}

void pr_session_name(int y, int mo, int d, int h, int mi, int s, char *out, size_t n)
{
    snprintf(out, n, "%04d%02d%02d-%02d%02d%02d", y, mo, d, h, mi, s);
}

void pr_frames_add(struct pr_frames *f, double ms)
{
    f->n++;
    f->sum_ms += ms;
    if (ms > f->worst_ms)
        f->worst_ms = ms;
    if (ms > 50.0)
        f->over50++;
    if (ms > 100.0)
        f->over100++;
}

double pr_frames_fps(const struct pr_frames *f)
{
    return f->n && f->sum_ms > 0.0 ? f->n * 1000.0 / f->sum_ms : 0.0;
}
