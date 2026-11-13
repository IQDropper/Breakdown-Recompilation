/* keymap.c - keyboard remapping text (A11Y-1). See keymap.h. */
#include "keymap.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *name, *guest, *label; } s_ctl[KEYMAP_CONTROLS] = {
    { "A", "A", "A" }, { "B", "B", "B" }, { "X", "X", "X" }, { "Y", "Y", "Y" },
    { "WHITE", "White", "White" }, { "BLACK", "Black", "Black (jump)" },
    { "LT", "Left trigger", "Left trigger" }, { "RT", "Right trigger", "Right trigger" },
    { "START", "START", "Start (pause)" }, { "BACK", "BACK", "Back" },
    { "LS", "Left stick click", "Left stick click (hands)" }, { "RS", "Right stick click", "Right stick click" },
    { "UP", "D-pad up", "D-pad up" }, { "DOWN", "D-pad down", "D-pad down" },
    { "LEFT", "D-pad left", "D-pad left" }, { "RIGHT", "D-pad right", "D-pad right" },
    { "LSUP", "Left stick up", "Left stick up (move)" }, { "LSDOWN", "Left stick down", "Left stick down (move)" },
    { "LSLEFT", "Left stick left", "Left stick left (move)" }, { "LSRIGHT", "Left stick right", "Left stick right (move)" },
    { "RSUP", "Right stick up", "Right stick up (look)" }, { "RSDOWN", "Right stick down", "Right stick down (look)" },
    { "RSLEFT", "Right stick left", "Right stick left (look)" }, { "RSRIGHT", "Right stick right", "Right stick right (look)" },
};

static const struct { const char *name; int vk; } s_keys[] = {
    { "SPACE", 0x20 }, { "TAB", 0x09 }, { "ENTER", 0x0D }, { "BACKSPACE", 0x08 }, { "SHIFT", 0x10 },
    { "CTRL", 0x11 }, { "ALT", 0x12 }, { "ESC", 0x1B }, { "CAPSLOCK", 0x14 },
    { "PAGEUP", 0x21 }, { "PAGEDOWN", 0x22 }, { "END", 0x23 }, { "HOME", 0x24 },
    { "LEFTARROW", 0x25 }, { "UPARROW", 0x26 }, { "RIGHTARROW", 0x27 }, { "DOWNARROW", 0x28 },
    { "INSERT", 0x2D }, { "DELETE", 0x2E },
    { "NUM0", 0x60 }, { "NUM1", 0x61 }, { "NUM2", 0x62 }, { "NUM3", 0x63 }, { "NUM4", 0x64 },
    { "NUM5", 0x65 }, { "NUM6", 0x66 }, { "NUM7", 0x67 }, { "NUM8", 0x68 }, { "NUM9", 0x69 },
    { "NUM*", 0x6A }, { "NUM+", 0x6B }, { "NUM-", 0x6D }, { "NUM.", 0x6E }, { "NUM/", 0x6F },
    { ";", 0xBA }, { "=", 0xBB }, { ",", 0xBC }, { "-", 0xBD }, { ".", 0xBE }, { "/", 0xBF }, { "`", 0xC0 },
    { "[", 0xDB }, { "\\", 0xDC }, { "]", 0xDD }, { "'", 0xDE },
};

static int eq(const char *a, const char *b)
{
    while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

int keymap_control(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < KEYMAP_CONTROLS; i++)
        if (eq(name, s_ctl[i].name))
            return i;
    return -1;
}

const char *keymap_control_name(int c) { return c >= 0 && c < KEYMAP_CONTROLS ? s_ctl[c].name : ""; }
const char *keymap_guest_name(int c) { return c >= 0 && c < KEYMAP_CONTROLS ? s_ctl[c].guest : ""; }
const char *keymap_control_label(int c) { return c >= 0 && c < KEYMAP_CONTROLS ? s_ctl[c].label : ""; }

int keymap_key(const char *name)
{
    size_t i, n;
    if (!name || !*name)
        return 0;
    n = strlen(name);
    if (n == 1 && isalnum((unsigned char)name[0]))
        return toupper((unsigned char)name[0]);   /* 'A'..'Z', '0'..'9' are their own VK codes */
    if ((name[0] == 'F' || name[0] == 'f') && n <= 3 && isdigit((unsigned char)name[1])) {
        int f = name[1] - '0';
        if (n == 3) {
            if (!isdigit((unsigned char)name[2]))
                return 0;
            f = f * 10 + (name[2] - '0');
        }
        return f >= 1 && f <= 12 ? 0x70 + f - 1 : 0;
    }
    for (i = 0; i < sizeof s_keys / sizeof s_keys[0]; i++)
        if (eq(name, s_keys[i].name))
            return s_keys[i].vk;
    return 0;
}

const char *keymap_key_name(int vk)
{
    static char buf[8];
    size_t i;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        buf[0] = (char)vk;
        buf[1] = 0;
        return buf;
    }
    if (vk >= 0x70 && vk <= 0x7B) {
        snprintf(buf, sizeof buf, "F%d", vk - 0x70 + 1);
        return buf;
    }
    for (i = 0; i < sizeof s_keys / sizeof s_keys[0]; i++)
        if (s_keys[i].vk == vk)
            return s_keys[i].name;
    return NULL;
}

static void trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
}

int keymap_parse(const char *text, struct keymap_bind binds[KEYMAP_CONTROLS], int *bad)
{
    char buf[512], *p, *next;
    int used = 0, nbad = 0;
    memset(binds, 0, sizeof(struct keymap_bind) * KEYMAP_CONTROLS);
    if (bad)
        *bad = 0;
    if (!text)
        return 0;
    snprintf(buf, sizeof buf, "%s", text);
    for (p = buf; p && *p; p = next) {
        char *eqs, *bar;
        int c, k0, k1 = 0;
        next = strchr(p, ';');
        if (next)
            *next++ = 0;
        trim(p);
        if (!*p)
            continue;
        eqs = strchr(p, '=');
        if (!eqs) {
            nbad++;
            continue;
        }
        *eqs++ = 0;
        trim(p);
        bar = strchr(eqs, '|');
        if (bar) {
            *bar++ = 0;
            trim(bar);
            k1 = keymap_key(bar);
        }
        trim(eqs);
        c = keymap_control(p);
        k0 = keymap_key(eqs);
        if (c < 0 || !k0 || (bar && !k1)) {
            nbad++;
            continue;
        }
        binds[c].set = 1;
        binds[c].vk[0] = (unsigned char)k0;
        binds[c].vk[1] = (unsigned char)(k1 == k0 ? 0 : k1);
        used++;
    }
    if (bad)
        *bad = nbad;
    return used;
}

int keymap_format(const struct keymap_bind binds[KEYMAP_CONTROLS], char *out, size_t n)
{
    size_t len = 0;
    int c, w;
    if (!out || !n)
        return -1;
    out[0] = 0;
    for (c = 0; c < KEYMAP_CONTROLS; c++) {
        const char *k0, *k1;
        if (!binds[c].set)
            continue;
        k0 = keymap_key_name(binds[c].vk[0]);
        if (!k0)
            continue;
        {
            char first[16];
            snprintf(first, sizeof first, "%s", k0);   /* keymap_key_name reuses its buffer */
            k1 = binds[c].vk[1] ? keymap_key_name(binds[c].vk[1]) : NULL;
            w = snprintf(out + len, n - len, "%s%s=%s%s%s", len ? ";" : "", s_ctl[c].name, first,
                         k1 ? "|" : "", k1 ? k1 : "");
        }
        if (w < 0 || (size_t)w >= n - len)
            return -1;
        len += (size_t)w;
    }
    return 0;
}
