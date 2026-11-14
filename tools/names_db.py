"""Function names database for the decomp (roadmap Phase 5).

Merges every source of function names into one file and counts progress:
  1. config/function_names.csv      - our hand names (always win)
  2. a Ghidra export functions.csv  - library names (XDK, D3D, CRT, DirectSound) and earlier game renames
  3. leaked source file names        - Ghidra names like ko_math_00106fb0 (function uses e:\\...\\kondoh\\ko_math.c)
  4. analysis/func_id/identified_functions.json - the auto sort by kind (render, audio, input...)

Nothing here changes generated code: the recompiler still emits sub_XXXXXXXX. The names are for people (and
later for a header the decomp can include).

  python tools/names_db.py [--ghidra PATH/functions.csv]
writes analysis/names/names.json and docs/decomp/NAMES_PROGRESS.md
"""
import argparse
import collections
import csv
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_GHIDRA = r"C:\Users\iron_\OneDrive\Emulation\RE\pipeline\out\07_named\functions.csv"

# Ghidra category -> our system name
GHIDRA_SYSTEM = {"render": "render", "crt": "crt", "audio": "audio", "thread": "xdk", "file": "xdk",
                 "system": "xdk", "memory": "xdk", "video": "video", "input": "xdk", "time": "xdk",
                 "other": "xdk", "game": None}
# identified_functions.json category -> system (a sort, not a name)
SORT_SYSTEM = {"game_render": "render", "game_audio": "audio", "game_input": "input", "game_debug": "debug",
               "game_io": "engine", "game_ui": "ui", "game_video": "video", "game_vehicle": "vehicle",
               "game_camera": "camera", "game_physics": "physics", "crt": "crt", "game_vtable": "objects"}
SRC_FILE = re.compile(r"^(ko_[a-z0-9_]+?)_[0-9a-f]{8}$")


def doubtful(name):
    """Ghidra function-ID guesses that are known to misfire on game code."""
    return name.startswith("FID_conflict") or name.startswith("?Afx") or name in ("default",)


def system_of_name(name):
    pre = name.split("_", 1)[0]
    return {"Data": "engine", "File": "engine", "FS": "engine", "Memory": "engine", "VsHeap": "engine",
            "VsMemory": "engine", "Heap": "engine", "Game": "engine", "System": "engine", "Common": "engine",
            "Area": "areas", "AreaCache": "areas", "EventPak": "areas", "Chara": "character",
            "Graphics": "render", "Render": "render", "Frame": "render", "FrameTimer": "render",
            "Movie": "video", "Stream": "audio", "Sound": "audio", "Input": "input",
            "Debug": "debug", "DebugPrintf": "debug"}.get(pre, "engine")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ghidra", default=DEFAULT_GHIDRA)
    ap.add_argument("--identified", default=None,
                    help="identified_functions.json (default: analysis/func_id/ here, else the main checkout)")
    args = ap.parse_args()

    ident = args.identified or os.path.join(ROOT, "analysis/func_id/identified_functions.json")
    if not os.path.exists(ident):
        ident = "D:/Emulation/Breakdown_Recomp/analysis/func_id/identified_functions.json"
    funcs = json.load(open(ident))
    db = {}
    for f in funcs:
        a = int(f["start"], 16)
        sysname = SORT_SYSTEM.get(f["category"])
        db[a] = {"address": f"0x{a:06X}", "name": None, "system": sysname, "source": "sorted" if sysname else
                 "unknown", "confidence": None, "evidence": f["method"] if sysname else ""}

    ghidra_n = 0
    if os.path.exists(args.ghidra):
        for r in csv.DictReader(open(args.ghidra, encoding="utf-8")):
            if r["named"] != "true":
                continue
            a = int(r["address"], 16)
            if a not in db:
                continue
            name = r["name"]
            m = SRC_FILE.match(name)
            e = db[a]
            if m:
                e.update(source="source-file", system=m.group(1), evidence=f"uses {m.group(1)}.c path string")
                continue
            if doubtful(name):
                e.update(evidence=f"Ghidra guess '{name}' (doubtful, not used)")
                continue
            sysname = GHIDRA_SYSTEM.get(r["category"]) or system_of_name(name)
            e.update(name=name, system=sysname, source="library" if r["category"] != "game" else "earlier-rename",
                     confidence="likely" if r["category"] == "game" else "sure", evidence="Ghidra 07_named")
            ghidra_n += 1
    else:
        print(f"no Ghidra export at {args.ghidra}: library names skipped")

    hand = 0
    with open(os.path.join(ROOT, "config/function_names.csv"), encoding="utf-8") as fh:
        rows = [ln for ln in fh if ln.strip() and not ln.startswith("#")]
    for r in csv.DictReader(rows):
        a = int(r["address"], 16)
        e = db.setdefault(a, {"address": f"0x{a:06X}"})
        e.update(name=r["name"], system=r["system"], source="hand", confidence=r["confidence"],
                 evidence=r["evidence"])
        hand += 1

    names = collections.Counter(e["name"] for e in db.values() if e.get("name"))
    dup = [n for n, c in names.items() if c > 1]
    hand_dup = [n for n in dup if any(e.get("name") == n and e["source"] == "hand" for e in db.values())]
    if hand_dup:
        raise SystemExit(f"duplicate hand names: {hand_dup}")
    for e in db.values():   # library copies (static CRT helpers linked twice): keep both, tell them apart
        if e.get("name") in dup:
            e["name"] = f"{e['name']}_{int(e['address'], 16):06X}"

    out = sorted(db.values(), key=lambda e: int(e["address"], 16))
    os.makedirs(os.path.join(ROOT, "analysis/names"), exist_ok=True)
    json.dump(out, open(os.path.join(ROOT, "analysis/names/names.json"), "w"), indent=0)

    total = len(out)
    by_src = collections.Counter(e["source"] for e in out)
    named = sum(1 for e in out if e.get("name"))
    game_named = sum(1 for e in out if e.get("name") and e["source"] in ("hand", "earlier-rename"))
    per = collections.defaultdict(collections.Counter)
    for e in out:
        per[e.get("system") or "(not sorted)"][e["source"]] += 1

    lines = ["# Function names: progress", "",
             "Made by `python tools/names_db.py` from config/function_names.csv (hand names), the Ghidra export",
             "(library names) and analysis/func_id/identified_functions.json (the auto sort). Do not edit by hand.", "",
             "| | Functions | Share |", "|---|---|---|"]
    def row(label, n):
        lines.append(f"| {label} | {n:,} | {100 * n / total:.1f}% |")
    row("**Named** (any source)", named)
    row("of which game code named by us or the earlier pass", game_named)
    row("of which Xbox library names (XDK, D3D, sound, C runtime)", by_src["library"])
    row("Original source file known (kondoh/ko_*.c), not named yet", by_src["source-file"])
    row("Sorted by kind only (render, audio, objects...)", by_src["sorted"])
    row("Nothing known", by_src["unknown"])
    row("**Total**", total)
    lines += ["", "## By system", "", "| System | Named by hand | Earlier pass | Library | Source file | Sorted only | Total |",
              "|---|---|---|---|---|---|---|"]
    for s, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
        lines.append(f"| {s} | {c['hand']} | {c['earlier-rename']} | {c['library']} | {c['source-file']} | "
                     f"{c['sorted']} | {sum(c.values()) - 0:,} |")
    os.makedirs(os.path.join(ROOT, "docs/decomp"), exist_ok=True)
    open(os.path.join(ROOT, "docs/decomp/NAMES_PROGRESS.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"{total} functions: {named} named ({hand} hand, {ghidra_n} from Ghidra), "
          f"{by_src['source-file']} source-file, {by_src['sorted']} sorted, {by_src['unknown']} unknown")


if __name__ == "__main__":
    main()
