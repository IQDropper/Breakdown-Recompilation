/*
 * recomp_env_game.h - Breakdown defaults for toolkit environment keys.
 *
 * xboxrecomp src/platform/recomp_env.h includes this file when
 * RECOMP_ENV_HAVE_GAME_KEYS is set on the recomp_env target (CMakeLists.txt).
 * A default applies only when neither the key nor its old variable is set.
 * Same pattern as BLiNX 2's src/env/recomp_env_game.h.
 */
#ifndef RECOMP_ENV_GAME_H
#define RECOMP_ENV_GAME_H

/*
 * D3D11_OCC=sync: answer visibility tests at GET_REPORT.
 *
 * The toolkit default is deferred. GET_REPORT then leaves the report status
 * at 0xFFFFFFFF (D3DERR_TESTINCOMPLETE) until a later poll, and a report
 * still pending after 250 ms is completed as visible. Breakdown uses those
 * tests for lights and overlays; deferred answers made them flicker on and
 * off. run_native.cmd already set RECOMP_D3D11_OCC=sync. This default covers
 * a direct launch of the exe, which never saw that variable.
 *
 * A/B the old path with RECOMP_D3D11_OCC=defer (run_native_occdefer.cmd).
 * RECOMP_D3D11_OCC=fixed forces every test visible. See
 * docs/flicker-investigation.md.
 */
#define RECOMP_ENV_GAME_DEFAULTS(D) \
    D(D3D11_OCC, "sync")

#endif /* RECOMP_ENV_GAME_H */
