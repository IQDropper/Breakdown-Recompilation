#!/usr/bin/env python3
"""First-run disc discovery, import, and launch check.

Looks for a disc image you own in two folders inside this clone, then
stops. A path you type or paste is only the fallback. This script never
downloads a disc image.

`breakdown import` extracts into game_files/ and checks the title id.
That reader is the CLI copy of the toolkit's tools.xiso / XDVDFS code.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from pathlib import Path

DATA_DIRS = ("romdata", "stream")
FAIL_MARKERS = (
    "Cannot open XBE",
    "Failed to load default.xbe",
    "Failed to initialize Xbox memory",
)
PASS_MARKERS = ("XBE loaded", "Starting game")
SMOKE_ENV = {
    "RECOMP_PB_BACKEND": "d3d11",
    "RECOMP_D3D11_OCC": "sync",
    "RECOMP_VBLANK": "1",
    "RECOMP_KEYBOARD": "1",
    "RECOMP_MOUSE_SENS": "1200",
    "RECOMP_MOUSE_INVERT": "0",
    "RECOMP_MOUSE_BUTTONS": "LT,RT,Y,WHITE,BLACK",
    "RECOMP_RENDER_SCALE": "2",
    "RECOMP_WINDOW_SCALE": "2",
    "RECOMP_DISPLAY_ASPECT": "16:9",
    "RECOMP_PRESENT_FILTER": "nearest",
}


def disc_rank(name: str) -> int | None:
    """Name class for auto-detect. None means "ask for a path instead".

    Only the common names a Breakdown dump actually uses. An unrelated
    *.iso in the folder (some other game) is left alone so the wizard
    does not grab it. The owner can still paste that path.
    """
    low = name.casefold()
    # *.xiso.iso before *.xiso before Breakdown*.iso, because the longer
    # suffix is its own kind of file (a wrapped XISO), not a plain ISO.
    if low.endswith(".xiso.iso"):
        return 2
    if low.endswith(".xiso"):
        return 1
    if low.startswith("breakdown") and low.endswith(".iso"):
        return 0
    return None


def _usa_bias(name: str) -> int:
    """Breakdown (USA) is the usual retail name, so it leads its own class."""
    return 0 if name.casefold().startswith("breakdown (usa).") else 1


def search_places(repo: Path) -> list[tuple[int, Path]]:
    """The only folders auto-detect will read. Not recursive.

    Place 0 is build-win, the same directory as breakdown_recomp.exe.
    Place 1 is the clone root, one folder up from that exe, next to
    FIRST_RUN.cmd. Both are inside the checkout the person just
    downloaded, so the normal step is "drop your file here", not "type
    an absolute path from some other drive".
    """
    root = Path(repo)
    return [(0, root / "build-win"), (1, root)]


def discover(repo: Path) -> list[Path]:
    """In-checkout disc images, best suggestion first.

    Resolution order, and why each step exists:

    1. build-win\\Breakdown*.iso — beside the exe. A self-contained
       clone: the disc sits in the same folder as the program.
    2. <repo>\\Breakdown*.iso — one folder up from the exe, next to
       FIRST_RUN.cmd. Same reason, for someone who has not built yet
       and only has the folder GitHub gave them.
    3. build-win\\*.xiso — same exe folder, XISO name.
    4. <repo>\\*.xiso — one folder up, XISO name.
    5. build-win\\*.xiso.iso — same exe folder, wrapped XISO name.
    6. <repo>\\*.xiso.iso — one folder up, wrapped XISO name.
    7. A path the user types or pastes, handled by choose_iso, when
       none of 1-6 match. That is the fallback, not the first ask.

    Name class wins over folder, so Breakdown (USA).iso next to
    FIRST_RUN.cmd beats a stray .xiso that happens to sit beside the
    exe. Inside one class, the exe folder wins over the clone root.
    """
    found: list[tuple[int, int, int, str, Path]] = []
    for place, directory in search_places(repo):
        if not directory.is_dir():
            continue
        try:
            children = list(directory.iterdir())
        except OSError:
            continue
        for path in children:
            # Files only, and only this directory. A nested isos\ folder
            # would send people hunting outside the two drop spots.
            if not path.is_file():
                continue
            rank = disc_rank(path.name)
            if rank is None:
                continue
            found.append((rank, place, _usa_bias(path.name), path.name.casefold(), path))
    found.sort()
    return [item[-1] for item in found]


def describe(repo: Path, path: Path) -> str:
    try:
        rel = path.resolve().relative_to(Path(repo).resolve())
    except ValueError:
        return "%s (a path you typed)" % path
    if rel.parent == Path("."):
        return "%s (this folder, next to FIRST_RUN.cmd)" % path
    if len(rel.parts) == 2 and rel.parts[0].casefold() == "build-win":
        return "%s (build-win, next to breakdown_recomp.exe)" % path
    return str(path)


def resolve_user_path(repo: Path, text: str) -> Path:
    raw = text.strip().strip('"').strip("'").strip()
    path = Path(raw)
    if not path.is_absolute():
        path = Path(repo) / path
    return path


def choose_iso(repo: Path, input_fn=None, output_fn=None) -> Path | None:
    """Suggest an in-repo disc image. Enter accepts it. A typed path is fallback."""
    repo = Path(repo)
    if input_fn is None:
        input_fn = input
    if output_fn is None:
        def output_fn(text, _write=sys.stderr.write):  # noqa: ARG001
            _write(text)
            sys.stderr.flush()

    while True:
        found = discover(repo)
        if found:
            output_fn("Disc images in this checkout:\n\n")
            output_fn("Suggested: %s\n\n" % describe(repo, found[0]))
            if len(found) > 1:
                output_fn("Also found:\n")
                for index, path in enumerate(found, 1):
                    note = "  (suggested)" if index == 1 else ""
                    output_fn("  %d. %s%s\n" % (index, describe(repo, path), note))
                output_fn("\n")
            output_fn("Press Enter to use the suggested file.\n")
            output_fn("Type a number to pick another listed file.\n")
            output_fn("Or paste a full path to a different ISO/XISO you own.\n")
            output_fn("Type q to quit.\n")
        else:
            # Nothing in the two in-clone folders. Say where to drop the
            # file (exe folder, then one up) before offering a paste.
            # Enter looks again. A pasted path is the fallback for a file
            # that is not in this checkout.
            exe_dir = Path(repo) / "build-win"
            output_fn("No Breakdown ISO/XISO in this checkout yet.\n\n")
            output_fn(
                "Copy the disc image you own into one of these folders, "
                "then press Enter to look again:\n\n"
            )
            output_fn("  %s\n" % exe_dir)
            output_fn("    next to breakdown_recomp.exe (same folder as the exe)\n\n")
            output_fn("  %s\n" % repo)
            output_fn("    one folder up from the exe, next to FIRST_RUN.cmd\n")
            output_fn("    example: Breakdown (USA).iso, a .xiso, or a .xiso.iso\n\n")
            output_fn("Or paste the full path to your ISO/XISO.\n")
            output_fn("Type q to quit.\n")
        try:
            line = input_fn("> ")
        except EOFError:
            return None
        if line is None:
            return None
        line = str(line).strip().strip('"').strip("'").strip()
        if line.casefold() == "q":
            return None
        if not line:
            # Enter looks again. A file dropped into build-win or the
            # clone root since the prompt is then the suggestion. When
            # the list was already on screen, Enter accepts the best one.
            fresh = discover(repo)
            if fresh:
                return fresh[0]
            continue
        if line.isdigit() and found:
            number = int(line)
            if 1 <= number <= len(found):
                return found[number - 1]
            output_fn("There is no file numbered %s.\n" % line)
            continue
        path = resolve_user_path(repo, line)
        if path.is_file() or path.is_dir():
            return path
        output_fn("Not found: %s\n" % line)
        output_fn("Put the file next to FIRST_RUN.cmd or in build-win, then press Enter.\n")


def read_manifest(repo: Path) -> dict:
    """game_files dir, XBE path, and title id from game.toml."""
    text = (Path(repo) / "game.toml").read_text(encoding="utf-8")
    section = None
    vals: dict[tuple[str, str], object] = {}
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            continue
        if section not in ("xbe", "data") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        key, val = key.strip(), val.strip()
        if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
            parsed: object = val[1:-1]
        elif val.lower().startswith("0x"):
            parsed = int(val, 16)
        else:
            parsed = val
        vals[(section, key)] = parsed
    return {
        "game_files": str(vals.get(("data", "game_files"), "game_files")),
        "xbe": str(vals.get(("xbe", "path"), "game_files/default.xbe")),
        "title_id": int(vals.get(("xbe", "title_id"), 0x4E4D0009)),
    }


def title_id_of(data: bytes) -> int:
    """Certificate title id. Same fields as xboxrecomp-cli's xbe.title_id."""
    if len(data) < 0x11C or data[:4] != b"XBEH":
        raise ValueError("not an XBE (no XBEH magic)")
    (base,) = struct.unpack_from("<I", data, 0x104)
    (cert,) = struct.unpack_from("<I", data, 0x118)
    off = cert - base
    if off < 0 or off + 12 > len(data):
        raise ValueError("certificate address outside the file")
    (tid,) = struct.unpack_from("<I", data, off + 8)
    return tid


def title_id_file(path: Path) -> int:
    with path.open("rb") as handle:
        head = handle.read(0x11C)
        if len(head) < 0x11C or head[:4] != b"XBEH":
            raise ValueError("not an XBE (no XBEH magic)")
        (base,) = struct.unpack_from("<I", head, 0x104)
        (cert,) = struct.unpack_from("<I", head, 0x118)
        off = cert - base
        if off < 0:
            raise ValueError("certificate address outside the file")
        handle.seek(off + 8)
        raw = handle.read(4)
        if len(raw) < 4:
            raise ValueError("certificate address outside the file")
        (tid,) = struct.unpack("<I", raw)
        return tid


def check_dump(repo: Path) -> list[tuple[str, str]]:
    """Pass/fail rows for game_files/default.xbe plus romdata and stream."""
    repo = Path(repo)
    try:
        manifest = read_manifest(repo)
    except OSError as exc:
        return [("fail", "game.toml: %s" % exc)]
    rows: list[tuple[str, str]] = []
    xbe = repo / manifest["xbe"]
    if not xbe.is_file():
        rows.append(("fail", "missing %s" % manifest["xbe"]))
        return rows
    try:
        tid = title_id_file(xbe)
    except (OSError, ValueError) as exc:
        rows.append(("fail", "%s: %s" % (manifest["xbe"], exc)))
        return rows
    want = manifest["title_id"]
    if tid != want:
        rows.append(
            (
                "fail",
                "title id 0x%08X does not match game.toml 0x%08X" % (tid, want),
            )
        )
    else:
        rows.append(("pass", "title id 0x%08X" % tid))
    gf = repo / manifest["game_files"]
    present: dict[str, str] = {}
    if gf.is_dir():
        for child in gf.iterdir():
            if child.is_dir():
                present[child.name.casefold()] = child.name
    for name in DATA_DIRS:
        found = present.get(name.casefold())
        if found:
            rows.append(("pass", "%s\\%s" % (manifest["game_files"], found)))
        else:
            rows.append(
                ("fail", "%s\\%s was not extracted" % (manifest["game_files"], name))
            )
    return rows


def dump_ok(rows: list[tuple[str, str]]) -> bool:
    return bool(rows) and all(status == "pass" for status, _msg in rows)


def interpret_launch(
    stdout: str, stderr: str, returncode: int | None, timed_out: bool
) -> tuple[str, str]:
    text = "%s\n%s" % (stdout, stderr)
    if any(marker in text for marker in FAIL_MARKERS):
        return "fail", "the exe reported it could not load the game files"
    if any(marker in text for marker in PASS_MARKERS):
        return "pass", "the exe loaded the XBE"
    if timed_out:
        return "fail", "the exe kept running without printing that the XBE loaded"
    return "fail", "the exe exited before it loaded the XBE (code %s)" % returncode


def smoke(repo: Path, seconds: int = 8, popen=None, platform_name: str | None = None):
    """Short native launch. Skip when the exe is not built yet."""
    repo = Path(repo)
    exe = repo / "build-win" / "breakdown_recomp.exe"
    if not exe.is_file():
        return (
            "skip",
            "build-win\\breakdown_recomp.exe is not built yet. "
            "Run breakdown all, then run_smoke.cmd.",
        )
    plat = sys.platform if platform_name is None else platform_name
    if plat != "win32":
        return (
            "skip",
            "native smoke runs on Windows, where breakdown_recomp.exe runs. "
            "Run breakdown all on Windows, then run_smoke.cmd.",
        )
    launcher = subprocess.Popen if popen is None else popen
    env = os.environ.copy()
    env.update(SMOKE_ENV)
    try:
        proc = launcher(
            [str(exe)],
            cwd=str(repo),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        return "fail", "could not start %s (%s)" % (exe, exc)
    timed_out = False
    try:
        out, err = proc.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
        timed_out = True
    stdout = out.decode("utf-8", errors="replace") if isinstance(out, bytes) else (out or "")
    stderr = err.decode("utf-8", errors="replace") if isinstance(err, bytes) else (err or "")
    try:
        (repo / "run_smoke_stdout.txt").write_text(stdout, encoding="utf-8")
        (repo / "run_smoke_stderr.txt").write_text(stderr, encoding="utf-8")
    except OSError:
        pass
    return interpret_launch(stdout, stderr, proc.returncode, timed_out)


def run_tool(repo: Path, args: list[str], runner=None) -> int:
    cmd = [sys.executable, str(Path(repo) / "breakdown.py"), *args]
    if runner is None:
        completed = subprocess.run(cmd, cwd=str(repo))
    else:
        completed = runner(cmd, cwd=str(repo))
    return int(completed.returncode)


def wizard(repo: Path, iso: Path, runner=None, smoke_fn=None) -> int:
    """Import one chosen image, then dump check, doctor, and optional smoke."""
    repo = Path(repo)
    iso = Path(iso)
    xiso_main = repo / "external" / "xboxrecomp" / "tools" / "xiso" / "__main__.py"
    if xiso_main.is_file():
        print(
            "Extract: breakdown import. tools.xiso is present at external\\xboxrecomp\\tools\\xiso.",
            flush=True,
        )
    else:
        print(
            "Extract: breakdown import (XDVDFS reader, title check). "
            "setup also places tools.xiso in external\\xboxrecomp.",
            flush=True,
        )
    print("Importing %s into game_files\\" % iso, flush=True)
    imported = run_tool(repo, ["import", str(iso)], runner)
    if imported == 0:
        print("[PASS] import", flush=True)
    else:
        print("[FAIL] import (exit %s)" % imported, flush=True)
        print("If game_files already holds a different dump, run:", flush=True)
        print('  breakdown import --replace "%s"' % iso, flush=True)

    rows = check_dump(repo)
    for status, message in rows:
        print("[%s] %s" % (status.upper(), message), flush=True)
    ready = dump_ok(rows)

    doctor = run_tool(repo, ["doctor"], runner)
    if doctor == 0:
        print("[PASS] doctor", flush=True)
    else:
        print("[FAIL] doctor (exit %s)" % doctor, flush=True)

    if imported != 0 or not ready:
        print("[SKIP] native smoke, because the dump is not ready", flush=True)
        smoke_status = "skip"
    else:
        status, message = (smoke_fn or smoke)(repo)
        smoke_status = status
        print("[%s] native smoke: %s" % (status.upper(), message), flush=True)

    ok = imported == 0 and ready and doctor == 0 and smoke_status != "fail"
    print("", flush=True)
    if ok:
        print("[PASS] First-run checks finished.", flush=True)
        if smoke_status == "skip":
            print("Next: breakdown all", flush=True)
            print("Then: run_native.cmd", flush=True)
    else:
        print("[FAIL] First-run checks found a problem.", flush=True)
    return 0 if ok else 1


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(prog="first_run.py")
    sub = parser.add_subparsers(dest="cmd", required=True)

    discover_p = sub.add_parser("discover", help="print the suggested in-repo disc image")
    discover_p.add_argument("--repo", default=".")

    choose_p = sub.add_parser("choose", help="suggest an in-repo image, or ask for a path")
    choose_p.add_argument("--repo", default=".")
    choose_p.add_argument("--save", help="write the chosen path as UTF-8")

    check_p = sub.add_parser("check-dump", help="check game_files title id, romdata, and stream")
    check_p.add_argument("--repo", default=".")

    smoke_p = sub.add_parser("smoke", help="short native exe launch, if it is already built")
    smoke_p.add_argument("--repo", default=".")
    smoke_p.add_argument("--seconds", type=int, default=8)

    wizard_p = sub.add_parser("wizard", help="import a chosen image, then check, doctor, and smoke")
    wizard_p.add_argument("--repo", default=".")
    wizard_p.add_argument("--path-file", required=True)

    args = parser.parse_args(argv)
    repo = Path(args.repo).resolve()

    if args.cmd == "discover":
        found = discover(repo)
        for path in found:
            print(describe(repo, path), file=sys.stderr)
        if not found:
            return 2
        print(found[0])
        return 0

    if args.cmd == "choose":
        picked = choose_iso(repo)
        if picked is None:
            return 2
        text = str(picked)
        if args.save:
            Path(args.save).write_text(text, encoding="utf-8")
        else:
            print(text)
        return 0

    if args.cmd == "check-dump":
        rows = check_dump(repo)
        for status, message in rows:
            print("[%s] %s" % (status.upper(), message))
        return 0 if dump_ok(rows) else 1

    if args.cmd == "smoke":
        status, message = smoke(repo, seconds=args.seconds)
        print("[%s] %s" % (status.upper(), message))
        return 1 if status == "fail" else 0

    raw = Path(args.path_file).read_text(encoding="utf-8").strip().strip('"').strip("'")
    if not raw:
        print("[FAIL] The disc path was empty.", file=sys.stderr)
        return 1
    return wizard(repo, Path(raw))


if __name__ == "__main__":
    sys.exit(main())
