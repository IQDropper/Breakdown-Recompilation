/* host_paths.h - where host features (speedrun splits, screenshots, tuning presets, play recordings, the
 * first-launch marker) keep their files.
 *
 * RECOMP_DATA_DIR set: there (test runs point it at a temp folder so they never touch the player's files).
 * Else %LOCALAPPDATA%\xboxrecomp, as before. Else .\xboxrecomp.
 * host_data_dir(out, n, "speedrun") gives <root>\speedrun and creates both folders; sub NULL gives the root. */
#ifndef HOST_PATHS_H
#define HOST_PATHS_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static inline void host_data_dir(char *out, size_t n, const char *sub)
{
    const char *dd = getenv("RECOMP_DATA_DIR"), *la = getenv("LOCALAPPDATA");
    if (dd && *dd)
        snprintf(out, n, "%s", dd);
    else
        snprintf(out, n, "%s\\xboxrecomp", la && *la ? la : ".");
    CreateDirectoryA(out, NULL);
    if (sub && *sub) {
        size_t len = strlen(out);
        snprintf(out + len, n > len ? n - len : 0, "\\%s", sub);
        CreateDirectoryA(out, NULL);
    }
}

#endif /* HOST_PATHS_H */
