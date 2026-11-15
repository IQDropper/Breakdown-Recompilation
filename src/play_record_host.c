/*
 * play_record_host.c - P2.1 play recorder (RECOMP_PLAY_RECORD=1), the Windows side. Pure parts: src/play_record.c.
 *
 * One folder per session: %LOCALAPPDATA%\xboxrecomp\play_sessions\<yyyymmdd-hhmmss>\ (RECOMP_DATA_DIR\play_sessions
 * when set; RECOMP_PLAY_RECORD_DIR overrides the parent folder):
 *   events.jsonl  one line per event, flushed at once so a crash keeps everything before it:
 *                 start, area (each new area/section, with the game's own name), save (a checkpoint save copied),
 *                 pos (the hero every 5 s), slow (a frame over 100 ms), fps (each minute: average, frames over
 *                 50/100 ms, worst), shot, mark, crash
 *   shots\        a PNG 3 s after each new section, one every 5 minutes, and one per mark
 *   saves\        start\ = the save folders as they were at launch; <hhmmss>_<slot>\ = a slot folder the game
 *                 wrote, copied 2 s after its last write. The player's saves are only read.
 * Mark key: F3 (screenshot + a "mark" line; the overlay says "mark saved").
 * Launch: run_record.cmd ("Play + Record") = run_native.cmd with RECOMP_PLAY_RECORD=1.
 * Screenshots are about 3.7 MB each (uncompressed PNG at 1280x960): roughly 50-60 MB an hour of play.
 * Saves live under RECOMP_SAVE_DIR, else RECOMP_DATA_DIR, else %LOCALAPPDATA%\xboxrecomp, in UDATA\.
 */
#include "play_record_host.h"
#include "play_record.h"
#include "qol_host.h"
#include "host_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

#define SHOT_AFTER_AREA_MS 3000.0
#define SHOT_EVERY_MS      (5.0 * 60.0 * 1000.0)
#define POS_EVERY_MS       5000.0
#define FPS_EVERY_MS       60000.0
#define SAVE_QUIET_MS      2000u
#define MAX_FILES          1024

static int s_on = -1;
static int s_ready;
static CRITICAL_SECTION s_lock;
static FILE *s_ev;
static char s_dir[MAX_PATH], s_save_root[MAX_PATH];
static LARGE_INTEGER s_freq, s_t0;
static char s_area[64], s_section[96];
static volatile float s_x, s_y, s_z;
static volatile int s_have_pos;
static int s_shots;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - s_t0.QuadPart) * 1000.0 / (double)s_freq.QuadPart;
}

int play_record_on(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_PLAY_RECORD");
        s_on = e && e[0] == '1';
    }
    return s_on;
}

/* One event line: {"t":12.3,"wall":"20:15:02","ev":"<ev>",<body>} */
static void event(const char *ev, const char *body)
{
    time_t w = time(NULL);
    struct tm *lt = localtime(&w);
    if (!s_ev)
        return;
    EnterCriticalSection(&s_lock);
    fprintf(s_ev, "{\"t\":%.1f,\"wall\":\"%02d:%02d:%02d\",\"ev\":\"%s\"%s%s}\n", now_ms() / 1000.0,
            lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, ev, body && *body ? "," : "",
            body ? body : "");
    fflush(s_ev);
    LeaveCriticalSection(&s_lock);
}

/* ,"area":"..","section":"..","x":..,"y":..,"z":.. for events that say where. */
static void where(char *out, size_t n)
{
    char a[96], s[160];
    pr_json_str(s_area, a, sizeof a);
    pr_json_str(s_section, s, sizeof s);
    if (s_have_pos)
        snprintf(out, n, "\"area\":\"%s\",\"section\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f", a, s, s_x, s_y, s_z);
    else
        snprintf(out, n, "\"area\":\"%s\",\"section\":\"%s\"", a, s);
}

static void shot(const char *why)
{
    char path[MAX_PATH], body[512], w[320], jp[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\shots\\%04d_%s.png", s_dir, ++s_shots, why);
    qol_shot_request(path);
    where(w, sizeof w);
    pr_json_str(path + strlen(s_dir) + 1, jp, sizeof jp);
    snprintf(body, sizeof body, "\"why\":\"%s\",\"file\":\"%s\",%s", why, jp, w);
    event("shot", body);
}

/* ---- saves: a polling thread, read-only on the player's folder ---- */

static struct { char path[MAX_PATH]; FILETIME wt; } s_files[MAX_FILES];
static int s_nfiles;

static void copy_tree(const char *from, const char *to)
{
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    HANDLE h;
    CreateDirectoryA(to, NULL);
    snprintf(pat, sizeof pat, "%s\\*", from);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName);
        snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            copy_tree(a, b);
        else
            CopyFileA(a, b, FALSE);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* Walks dir; a file new or newer than last time adds its folder to changed (a slot folder). */
static void scan(const char *dir, int first, char changed[][MAX_PATH], int *nchanged)
{
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH], p[MAX_PATH];
    HANDLE h;
    int i;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan(p, first, changed, nchanged);
            continue;
        }
        for (i = 0; i < s_nfiles && strcmp(s_files[i].path, p); i++)
            ;
        if (i == s_nfiles) {
            if (s_nfiles == MAX_FILES)
                continue;
            s_nfiles++;
            snprintf(s_files[i].path, sizeof s_files[i].path, "%s", p);
        } else if (!CompareFileTime(&s_files[i].wt, &fd.ftLastWriteTime)) {
            continue;
        }
        s_files[i].wt = fd.ftLastWriteTime;
        if (!first && *nchanged < 8) {
            int k;
            for (k = 0; k < *nchanged && strcmp(changed[k], dir); k++)
                ;
            if (k == *nchanged)
                snprintf(changed[(*nchanged)++], MAX_PATH, "%s", dir);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static DWORD WINAPI save_thread(LPVOID unused)
{
    static char pending[8][MAX_PATH];
    char udata[MAX_PATH], start[MAX_PATH];
    int npending = 0;
    DWORD last_change = 0;
    (void)unused;
    snprintf(udata, sizeof udata, "%s\\UDATA", s_save_root);
    snprintf(start, sizeof start, "%s\\saves\\start", s_dir);
    copy_tree(udata, start);
    scan(udata, 1, NULL, &npending);
    npending = 0;
    for (;;) {
        char changed[8][MAX_PATH];
        int n = 0, i, k;
        Sleep(500);
        scan(udata, 0, changed, &n);
        for (i = 0; i < n; i++) {
            for (k = 0; k < npending && strcmp(pending[k], changed[i]); k++)
                ;
            if (k == npending && npending < 8)
                snprintf(pending[npending++], MAX_PATH, "%s", changed[i]);
        }
        if (n)
            last_change = GetTickCount();
        if (npending && GetTickCount() - last_change >= SAVE_QUIET_MS) {
            time_t w = time(NULL);
            struct tm *lt = localtime(&w);
            for (i = 0; i < npending; i++) {
                const char *slot = strrchr(pending[i], '\\');
                char to[MAX_PATH], body[640], wh[320], js[MAX_PATH * 2];
                slot = slot ? slot + 1 : pending[i];
                snprintf(to, sizeof to, "%s\\saves\\%02d%02d%02d_%s", s_dir, lt ? lt->tm_hour : 0,
                         lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, slot);
                copy_tree(pending[i], to);
                where(wh, sizeof wh);
                pr_json_str(to + strlen(s_dir) + 1, js, sizeof js);
                snprintf(body, sizeof body, "\"copy\":\"%s\",%s", js, wh);
                event("save", body);
            }
            npending = 0;
        }
    }
    return 0;
}

/* ---- start ---- */

static void start(void)
{
    const char *la = getenv("LOCALAPPDATA"), *d = getenv("RECOMP_PLAY_RECORD_DIR");
    const char *sv = getenv("RECOMP_SAVE_DIR"), *dd = getenv("RECOMP_DATA_DIR");
    char parent[MAX_PATH], name[32], sub[MAX_PATH], body[1200], jd[MAX_PATH * 2], js[MAX_PATH * 2];
    time_t w = time(NULL);
    struct tm *lt = localtime(&w);
    HANDLE t;

    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_t0);
    InitializeCriticalSection(&s_lock);
    if (d && *d) {
        snprintf(parent, sizeof parent, "%s", d);
    } else {
        host_data_dir(parent, sizeof parent, "play_sessions");   /* RECOMP_DATA_DIR\play_sessions when set */
    }
    CreateDirectoryA(parent, NULL);
    pr_session_name(lt ? lt->tm_year + 1900 : 0, lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0, lt ? lt->tm_hour : 0,
                    lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, name, sizeof name);
    snprintf(s_dir, sizeof s_dir, "%s\\%s", parent, name);
    if (!CreateDirectoryA(s_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "[PLAY-REC] could not make %s; recorder off\n", s_dir);
        s_on = 0;
        return;
    }
    snprintf(sub, sizeof sub, "%s\\shots", s_dir);
    CreateDirectoryA(sub, NULL);
    snprintf(sub, sizeof sub, "%s\\saves", s_dir);
    CreateDirectoryA(sub, NULL);
    snprintf(sub, sizeof sub, "%s\\events.jsonl", s_dir);
    s_ev = fopen(sub, "a");
    if (!s_ev) {
        fprintf(stderr, "[PLAY-REC] could not open %s; recorder off\n", sub);
        s_on = 0;
        return;
    }
    if (sv && *sv)
        snprintf(s_save_root, sizeof s_save_root, "%s", sv);
    else if (dd && *dd)
        snprintf(s_save_root, sizeof s_save_root, "%s", dd);
    else
        snprintf(s_save_root, sizeof s_save_root, "%s\\xboxrecomp", la && *la ? la : ".");
    s_ready = 1;
    pr_json_str(s_dir, jd, sizeof jd);
    pr_json_str(s_save_root, js, sizeof js);
    snprintf(body, sizeof body, "\"folder\":\"%s\",\"saves\":\"%s\",\"mark_key\":\"F3\"", jd, js);
    event("start", body);
    fprintf(stderr, "[PLAY-REC] recording to %s (F3 = mark)\n", s_dir);
    fflush(stderr);
    t = CreateThread(NULL, 0, save_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

/* ---- per frame ---- */

static int game_in_front(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static double s_area_shot_at = -1.0;

void play_record_frame(void)
{
    static int started;
    static double last, next_pos, next_fps = FPS_EVERY_MS, next_periodic = SHOT_EVERY_MS;
    static struct pr_frames fr;
    static int was_mark;
    double t;
    int down;
    if (!play_record_on())
        return;
    if (!started) {
        started = 1;
        start();
    }
    if (!s_ready)
        return;
    t = now_ms();
    if (last > 0.0) {
        double dt = t - last;
        pr_frames_add(&fr, dt);
        if (dt > 100.0) {
            char body[400], w[320];
            where(w, sizeof w);
            snprintf(body, sizeof body, "\"ms\":%.1f,%s", dt, w);
            event("slow", body);
        }
    }
    last = t;
    if (t >= next_fps) {
        char body[200];
        snprintf(body, sizeof body, "\"fps\":%.1f,\"frames\":%u,\"over50\":%u,\"over100\":%u,\"worst_ms\":%.1f",
                 pr_frames_fps(&fr), fr.n, fr.over50, fr.over100, fr.worst_ms);
        event("fps", body);
        memset(&fr, 0, sizeof fr);
        next_fps = t + FPS_EVERY_MS;
    }
    if (s_have_pos && t >= next_pos) {
        char w[320];
        where(w, sizeof w);
        event("pos", w);
        next_pos = t + POS_EVERY_MS;
    }
    if (s_area_shot_at >= 0.0 && t >= s_area_shot_at) {
        s_area_shot_at = -1.0;
        shot("area");
        next_periodic = t + SHOT_EVERY_MS;
    } else if (t >= next_periodic) {
        shot("periodic");
        next_periodic = t + SHOT_EVERY_MS;
    }
    down = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    if (down && !was_mark && game_in_front()) {
        char w[320];
        shot("mark");
        where(w, sizeof w);
        event("mark", w);
        qol_note("mark saved");
    }
    was_mark = down;
}

void play_record_msg(const char *msg)
{
    char area[64], section[96];
    if (!play_record_on() || !s_ready || !pr_area_from_msg(msg, area, sizeof area, section, sizeof section))
        return;
    if (!section[0] && !strcmp(area, s_area))
        return;   /* "AREA DATA SET" for the area already known */
    if (!strcmp(area, s_area) && !strcmp(section, s_section))
        return;   /* the game repeats AREA_NAME_SET */
    {
        char body[512], a[96], s[160], ti[96];
        const char *title = pr_area_title(area);
        int new_area = strcmp(area, s_area) != 0;
        snprintf(s_area, sizeof s_area, "%s", area);
        if (section[0] || new_area)
            snprintf(s_section, sizeof s_section, "%s", section);
        pr_json_str(area, a, sizeof a);
        pr_json_str(section, s, sizeof s);
        pr_json_str(title ? title : area, ti, sizeof ti);
        snprintf(body, sizeof body, "\"area\":\"%s\",\"section\":\"%s\",\"title\":\"%s\",\"new_area\":%s", a, s, ti,
                 new_area ? "true" : "false");
        event("area", body);
        s_area_shot_at = now_ms() + SHOT_AFTER_AREA_MS;
    }
}

void play_record_hero(float x, float y, float z)
{
    s_x = x;
    s_y = y;
    s_z = z;
    s_have_pos = 1;
}

void play_record_crash(uint64_t rip, uint64_t fault, uint32_t guest_va, int write)
{
    char w[320];
    if (!s_ready || !s_ev)
        return;
    where(w, sizeof w);
    /* no lock: the crashing thread may hold it; a torn line is better than no line */
    fprintf(s_ev, "{\"t\":%.1f,\"ev\":\"crash\",\"rip\":\"0x%llX\",\"fault\":\"0x%llX\",\"guest_va\":\"0x%08X\","
            "\"access\":\"%s\",%s}\n", now_ms() / 1000.0, (unsigned long long)rip, (unsigned long long)fault,
            (unsigned)guest_va, write ? "write" : "read", w);
    fflush(s_ev);
}
