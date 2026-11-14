"""map_list.py - rebuild the developers' map list for the built-in MAP VIEWER (F8 -> MAKINO MENU -> MAP VIEWER).

The map viewer (0x1F880) lists the area table at [0x2A8E38] (entries of 0x50 bytes: area name, block letter,
title, start x y z) and, on a pick, loads that area with a free-fly debug camera (0x1F5D0). Retail ships no table,
so the list was empty. This writes one line per area section, read from the game's own level files:

    <area> <block> <x> <y> <z> <title>

Usage: python tools/map_list.py [--out build-win/map_list.txt]
src/map_list_host.c loads it when RECOMP_DEBUG_MENU=1 and F8 opens the developers' menu.
Reads game_files/ (your own copy). The output holds names and coordinates only; it stays out of git (build-win/).
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import level_catalog as lc  # noqa: E402

# Names as the game shows them (Foster, 2026-11-14) or as the game's text bank has them; folder name otherwise.
TITLES = {"opening": "Prologue", "hon_under": "7F Main Research Center", "bettou": "Annex",
          "hon_hon": "Main Hospital", "gesui": "Sewers", "kyodo1": "Utility Tunnel"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "build-win", "map_list.txt"))
    args = ap.parse_args()
    cat = lc.catalog()
    rows = []
    for area, a in cat.items():
        if area in ("extra_back",):
            continue
        blocks = {}
        for ld in a.get("loads", []):
            m = re.match(rf"^{re.escape(area)}_([A-Z])\.evd$", ld["file"])
            if m and m.group(1) not in blocks:
                blocks[m.group(1)] = ld["at"]
        for sp in a.get("spawns", []):
            for b in (sp.get("block") or ""):
                if b.isalpha() and b not in blocks and sp.get("at"):
                    blocks[b] = sp["at"]
        if not blocks:
            pos = next((ld["at"] for ld in a.get("loads", [])), None) or \
                  next((sp["at"] for sp in a.get("spawns", []) if sp.get("at")), [0.0, 0.0, 0.0])
            blocks["A"] = pos
        title = TITLES.get(area, area)
        for b in sorted(blocks):
            x, y, z = blocks[b]
            rows.append(f"{area} {b} {x:.1f} {y:.1f} {z:.1f} {title} {b}")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="ascii", errors="replace") as f:
        f.write("\n".join(rows) + "\n")
    print(f"{len(rows)} map entries -> {args.out}")


if __name__ == "__main__":
    main()
