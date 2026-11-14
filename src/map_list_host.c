/* map_list_host.c - gives the developers' MAP VIEWER its map list back.
 *
 * F8 -> MAKINO MENU -> MAP VIEWER (0x1F880) lists the area table at [0x2A8E38] (count [0x2A8E3C]) through
 * 0x2BE10 and, on a pick, starts a free-fly sample mode in that area (0x1F5D0 -> 0x2C130 -> Area_Load 0x2BFC0).
 * The table is a pointer to a pointer to entries of 0x50 bytes:
 *   +0x00 area folder name (Area_Load sprintf "area/%s"), +0x17 block letter ("BLK%c"),
 *   +0x18 title (printed after the position), +0x40/+0x44/+0x48 start x y z, +0x4C (copied, 0 here).
 * Retail never fills it (no code writes 0x2A8E38), so the list was empty. tools/map_list.py writes map_list.txt
 * beside the exe from the game's own level files; this copies it into guest memory when F8 opens the menu.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "gen/recomp_types.h"
#include "map_list_host.h"

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define MAP_TABLE_PTR   0x2A8E38u
#define MAP_TABLE_COUNT 0x2A8E3Cu
#define MAP_ENTRY_SIZE  0x50u
#define MAP_MAX         256

static void put_str(uint32_t va, const char *s, size_t max)
{
    size_t i;
    for (i = 0; i + 1 < max && s[i]; i++)
        MEM8(va + i) = (uint8_t)s[i];
    for (; i < max; i++)
        MEM8(va + i) = 0;
}

static void put_f32(uint32_t va, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    MEM32(va) = u;
}

int map_list_install(void)
{
    static int s_done;
    char path[MAX_PATH], line[256], area[32], title[96];
    char blk;
    float x, y, z;
    FILE *f;
    uint32_t cell, base;
    int n = 0, i;
    struct { char area[24]; char blk; char title[40]; float x, y, z; } rows[MAP_MAX];

    if (s_done)
        return (int)MEM32(MAP_TABLE_COUNT);
    if (MEM32(MAP_TABLE_PTR) != 0) {   /* the game has its own table: leave it */
        s_done = 1;
        return (int)MEM32(MAP_TABLE_COUNT);
    }
    {
        const char *e = getenv("RECOMP_MAP_LIST");
        if (e && *e) {
            snprintf(path, sizeof path, "%s", e);
        } else {
            char *slash;
            GetModuleFileNameA(NULL, path, sizeof path);
            slash = strrchr(path, '\\');
            if (slash)
                slash[1] = 0;
            strncat(path, "map_list.txt", sizeof path - strlen(path) - 1);
        }
    }
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[MAP-LIST] %s not found: run python tools/map_list.py (the MAP VIEWER list stays empty)\n",
                path);
        fflush(stderr);
        s_done = 1;
        return 0;
    }
    while (n < MAP_MAX && fgets(line, sizeof line, f)) {
        int used = 0;
        if (sscanf(line, "%31s %c %f %f %f %n", area, &blk, &x, &y, &z, &used) < 5)
            continue;
        snprintf(title, sizeof title, "%s", line + used);
        title[strcspn(title, "\r\n")] = 0;
        snprintf(rows[n].area, sizeof rows[n].area, "%s", area);
        snprintf(rows[n].title, sizeof rows[n].title, "%s", title);
        rows[n].blk = blk;
        rows[n].x = x; rows[n].y = y; rows[n].z = z;
        n++;
    }
    fclose(f);
    s_done = 1;
    if (n == 0)
        return 0;
    cell = xbox_HeapAlloc(16, 16);
    base = xbox_HeapAlloc((uint32_t)n * MAP_ENTRY_SIZE, 16);
    if (!cell || !base) {
        fprintf(stderr, "[MAP-LIST] no guest memory for %d entries\n", n);
        fflush(stderr);
        return 0;
    }
    for (i = 0; i < n; i++) {
        uint32_t e = base + (uint32_t)i * MAP_ENTRY_SIZE;
        put_str(e, rows[i].area, 0x17);
        MEM8(e + 0x17) = (uint8_t)rows[i].blk;
        put_str(e + 0x18, rows[i].title, 0x28);
        put_f32(e + 0x40, rows[i].x);
        put_f32(e + 0x44, rows[i].y);
        put_f32(e + 0x48, rows[i].z);
        MEM32(e + 0x4C) = 0;
    }
    MEM32(cell) = base;
    MEM32(MAP_TABLE_PTR) = cell;
    MEM32(MAP_TABLE_COUNT) = (uint32_t)n;
    fprintf(stderr, "[MAP-LIST] %d maps from %s for the MAP VIEWER\n", n, path);
    fflush(stderr);
    return n;
}
