/* speedrun_theme.c - speedrun_theme.ini reader (see speedrun_theme.h). */
#include "speedrun_theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sr_theme_default(struct sr_theme *t)
{
    memset(t, 0, sizeof *t);
    snprintf(t->font, sizeof t->font, "Consolas");
    t->size = 100;
    t->bold = 1;
    t->color_running = 0x78E678;   /* RGB(120, 230, 120) */
    t->color_stopped = 0xB4B4B4;   /* RGB(180, 180, 180) */
    t->color_text = 0xEBEBEB;      /* RGB(235, 235, 235) */
    t->color_title = 0xEBEBEB;
    t->color_box = 0x0C0C10;       /* RGB(12, 12, 16) */
    t->box = 1;
    t->opacity = 84;               /* alpha 215 of 255 */
    t->corner = SR_TOP_RIGHT;
    t->show_real = 1;
    t->show_split = 1;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    if (i)
        memmove(s, s + i, n - i + 1);
}

static void lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* "#RRGGBB", "RRGGBB" or "r, g, b". */
static int parse_color(const char *v, uint32_t *out)
{
    unsigned r, g, b;
    char extra;
    if (*v == '#')
        v++;
    if (strlen(v) == 6) {
        char *end;
        unsigned long x = strtoul(v, &end, 16);
        if (*end == 0) {
            *out = (uint32_t)x;
            return 1;
        }
    }
    if (sscanf(v, "%u , %u , %u %c", &r, &g, &b, &extra) == 3 && r < 256 && g < 256 && b < 256) {
        *out = (uint32_t)(r << 16 | g << 8 | b);
        return 1;
    }
    return 0;
}

static int parse_bool(const char *v, int *out)
{
    char s[16];
    snprintf(s, sizeof s, "%s", v);
    lower(s);
    if (!strcmp(s, "yes") || !strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "1")) {
        *out = 1;
        return 1;
    }
    if (!strcmp(s, "no") || !strcmp(s, "off") || !strcmp(s, "false") || !strcmp(s, "0")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int parse_int(const char *v, int lo, int hi, int *out)
{
    char *end;
    long x = strtol(v, &end, 10);
    if (end == v)
        return 0;
    while (*end == '%' || isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;
    *out = (int)(x < lo ? lo : x > hi ? hi : x);
    return 1;
}

static int parse_corner(const char *v, enum sr_corner *out)
{
    char s[32];
    size_t i;
    snprintf(s, sizeof s, "%s", v);
    lower(s);
    for (i = 0; s[i]; i++)
        if (s[i] == '_' || s[i] == ' ')
            s[i] = '-';
    if (!strcmp(s, "top-right")) *out = SR_TOP_RIGHT;
    else if (!strcmp(s, "top-left")) *out = SR_TOP_LEFT;
    else if (!strcmp(s, "bottom-right")) *out = SR_BOTTOM_RIGHT;
    else if (!strcmp(s, "bottom-left")) *out = SR_BOTTOM_LEFT;
    else return 0;
    return 1;
}

/* Quotes around a value are optional: title = "Any% (glitchless)" keeps the spaces inside. */
static void unquote(char *v)
{
    size_t n = strlen(v);
    if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') || (v[0] == '\'' && v[n - 1] == '\''))) {
        memmove(v, v + 1, n - 2);
        v[n - 2] = 0;
    }
}

static int setting(struct sr_theme *t, const char *key, const char *v)
{
    if (!strcmp(key, "title")) { snprintf(t->title, sizeof t->title, "%s", v); return 1; }
    if (!strcmp(key, "font")) { if (!*v) return 0; snprintf(t->font, sizeof t->font, "%s", v); return 1; }
    if (!strcmp(key, "size")) return parse_int(v, 50, 300, &t->size);
    if (!strcmp(key, "bold")) return parse_bool(v, &t->bold);
    if (!strcmp(key, "color_running")) return parse_color(v, &t->color_running);
    if (!strcmp(key, "color_stopped")) return parse_color(v, &t->color_stopped);
    if (!strcmp(key, "color_text")) return parse_color(v, &t->color_text);
    if (!strcmp(key, "color_title")) return parse_color(v, &t->color_title);
    if (!strcmp(key, "color_box")) return parse_color(v, &t->color_box);
    if (!strcmp(key, "box")) return parse_bool(v, &t->box);
    if (!strcmp(key, "opacity")) return parse_int(v, 10, 100, &t->opacity);
    if (!strcmp(key, "corner")) return parse_corner(v, &t->corner);
    if (!strcmp(key, "show_real_time")) return parse_bool(v, &t->show_real);
    if (!strcmp(key, "show_split")) return parse_bool(v, &t->show_split);
    return 0;
}

static void add_name(struct sr_theme *t, const char *id, const char *name)
{
    int i;
    for (i = 0; i < t->nnames; i++)
        if (!strcmp(t->names[i].id, id))
            break;
    if (i == t->nnames) {
        if (t->nnames >= SR_THEME_MAX_NAMES)
            return;
        t->nnames++;
    }
    snprintf(t->names[i].id, sizeof t->names[i].id, "%s", id);
    snprintf(t->names[i].name, sizeof t->names[i].name, "%s", name);
}

int sr_theme_parse(struct sr_theme *t, const char *text)
{
    int bad = 0, in_splits = 0;
    const char *p = text;
    /* a UTF-8 byte order mark (Notepad adds one) */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p) {
        char line[256], *eq, *c;
        size_t n = strcspn(p, "\r\n");
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line ? n : sizeof line - 1), p);
        p += n;
        while (*p == '\r' || *p == '\n')
            p++;
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[') {
            lower(line);
            in_splits = !strcmp(line, "[splits]");
            if (!in_splits && strcmp(line, "[timer]"))
                bad++;
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            bad++;
            continue;
        }
        *eq = 0;
        trim(line);
        c = eq + 1;
        trim(c);
        if (c[0] == '"' || c[0] == '\'') {
            /* quoted: everything inside is kept, anything after the closing quote is a comment */
            char *q = strchr(c + 1, c[0]);
            if (q)
                q[1] = 0;
        } else {
            /* a comment after the value: " # ..." (a '#' at the start, as in a color, is the value) */
            char *h = c;
            while ((h = strpbrk(h, "#;")) != NULL) {
                if (h > c && isspace((unsigned char)h[-1])) {
                    *h = 0;
                    break;
                }
                h++;
            }
        }
        trim(c);
        unquote(c);
        if (in_splits) {
            if (line[0])
                add_name(t, line, c);
            else
                bad++;
            continue;
        }
        lower(line);
        if (!setting(t, line, c))
            bad++;
    }
    return bad;
}

void sr_theme_split_name(const struct sr_theme *t, const char *split_area, const char *(*fallback)(const char *area),
                         char *out, size_t n)
{
    char area[48];
    size_t len = strlen(split_area);
    const char *f;
    int i;
    for (i = 0; t && i < t->nnames; i++)
        if (!strcmp(t->names[i].id, split_area)) {
            snprintf(out, n, "%s", t->names[i].name);
            return;
        }
    /* "hon_under_A" -> "hon_under" */
    snprintf(area, sizeof area, "%s", split_area);
    if (len > 2 && area[len - 2] == '_')
        area[len - 2] = 0;
    for (i = 0; t && i < t->nnames; i++)
        if (!strcmp(t->names[i].id, area)) {
            snprintf(out, n, "%s", t->names[i].name);
            return;
        }
    f = fallback ? fallback(area) : NULL;
    snprintf(out, n, "%s", f ? f : split_area);
}
