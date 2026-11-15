"""Session report (P2.2): one Markdown report from one play-recorder session folder.

    py -3 tools/play_report.py SESSION_DIR [--out reports/play/<name>.md]

SESSION_DIR is what run_record.cmd writes (src/play_record_host.c): events.jsonl (one JSON object per line:
start, area, save, pos, slow, fps, shot, mark, crash), shots\\, saves\\. The report has a summary table, a crash
box at the top if the game crashed, then one section per area/section visit in the order they happened:
time spent, entry time, screenshots, marks, slow frames, frame rate per minute and save copies.

A crash can leave the last line cut short; such a line is skipped (a cut crash line is still reported).
Stdlib only.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

# The game's own names for the areas Foster knows (same table as pr_area_title in src/play_record.c).
AREA_TITLES = {
    "hon_under": "7F / Main Research Center",
    "bettou": "Annex",
    "hon_hon": "Main Hospital",
    "gesui": "Sewers",
    "kyodo": "Utility Tunnel",
}

SLOW_MS = 100.0  # the recorder logs a "slow" event for any frame over this


# ---- reading ----

def read_events(path):
    """Returns (events, problems). Bad lines are skipped; a torn crash line is rebuilt from what is left."""
    events, problems = [], []
    try:
        raw = Path(path).read_bytes().decode("utf-8", "replace")
    except FileNotFoundError:
        return [], ["no events.jsonl in the session folder"]
    lines = raw.split("\n")
    for i, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            ev = json.loads(line)
            if isinstance(ev, dict) and "ev" in ev:
                events.append(ev)
                continue
            problems.append("line %d is not an event; skipped" % i)
        except ValueError:
            last = i == len(lines) or all(not x.strip() for x in lines[i:])
            if '"ev":"crash"' in line.replace(" ", ""):
                ev = {"ev": "crash", "torn": True}
                for k, v in re.findall(r'"(\w+)":"([^"]*)"', line):
                    ev.setdefault(k, v)
                for k, v in re.findall(r'"(\w+)":(-?[0-9.]+)', line):
                    try:
                        ev.setdefault(k, float(v))
                    except ValueError:
                        pass
                events.append(ev)
                problems.append("line %d (crash) was cut short; kept what could be read" % i)
            elif last:
                problems.append("last line %d was cut short (game stopped mid-write); skipped" % i)
            else:
                problems.append("line %d could not be read; skipped" % i)
    return events, problems


# ---- shaping ----

class Visit:
    def __init__(self, area, section, title, t, wall):
        self.area, self.section, self.title = area, section, title
        self.t0, self.t1, self.wall = t, t, wall
        self.shots, self.marks, self.slow, self.fps, self.saves, self.crashes = [], [], [], [], [], []

    @property
    def name(self):
        if not self.area:
            return "Before the first area (menus, loading)"
        n = self.title or AREA_TITLES.get(self.area, self.area)
        return "%s, section %s" % (n, self.section) if self.section else n


def tnum(ev):
    try:
        return float(ev.get("t", 0.0))
    except (TypeError, ValueError):
        return 0.0


def build(events):
    """Splits the events into visits (a new one at every area event). Returns (visits, info)."""
    events = sorted(events, key=tnum)  # already in order; a stable sort keeps same-time events as written
    info = {"start": None, "t_first": None, "t_last": None}
    visits = []
    cur = Visit("", "", "", 0.0, "")
    pending_mark_shot = None
    last_pos = None
    for ev in events:
        t, kind = tnum(ev), ev.get("ev")
        if info["t_first"] is None:
            info["t_first"] = t
        info["t_last"] = t
        if kind == "start":
            info["start"] = ev
            cur.t0 = cur.t1 = t
            cur.wall = cur.wall or ev.get("wall", "")
            continue
        if kind == "area":
            cur.t1 = t
            visits.append(cur)
            cur = Visit(ev.get("area", ""), ev.get("section", ""), ev.get("title", ""), t, ev.get("wall", ""))
            continue
        cur.t1 = t
        if kind == "shot":
            cur.shots.append(ev)
            if ev.get("why") == "mark":
                pending_mark_shot = ev
        elif kind == "mark":
            ev = dict(ev)
            if pending_mark_shot is not None and t - tnum(pending_mark_shot) <= 2.0:
                ev["shot"] = pending_mark_shot.get("file")
            pending_mark_shot = None
            cur.marks.append(ev)
        elif kind == "slow":
            cur.slow.append(ev)
        elif kind == "fps":
            cur.fps.append(ev)
        elif kind == "save":
            cur.saves.append(ev)
        elif kind == "crash":
            if pos(ev) == "unknown" and last_pos is not None:
                ev = dict(ev, last_pos=pos(last_pos))
            cur.crashes.append(ev)
        if kind != "crash" and pos(ev) != "unknown":
            last_pos = ev
    visits.append(cur)
    # The pre-area bucket is only worth a section if something happened in it.
    v0 = visits[0]
    if not v0.area and not (v0.shots or v0.marks or v0.slow or v0.fps or v0.saves or v0.crashes):
        visits.pop(0)
    return visits, info


# ---- writing ----

def clock(sec):
    sec = max(0, int(round(sec)))
    h, m, s = sec // 3600, sec // 60 % 60, sec % 60
    return "%d:%02d:%02d" % (h, m, s) if h else "%d:%02d" % (m, s)


def pos(ev):
    if all(k in ev for k in ("x", "y", "z")):
        return "%.1f, %.1f, %.1f" % (float(ev["x"]), float(ev["y"]), float(ev["z"]))
    return "unknown"


def crash_pos(ev):
    if "last_pos" in ev:
        return "unknown (last known: %s)" % ev["last_pos"]
    return pos(ev)


def link(session, out_dir, rel):
    """Markdown link target for a file inside the session folder, relative to the report."""
    p = (Path(session) / rel.replace("\\", "/")).resolve()
    try:
        r = os.path.relpath(p, out_dir.resolve())
    except ValueError:  # another drive on Windows
        return p.as_uri()
    return r.replace(os.sep, "/").replace(" ", "%20")


def where_text(ev):
    a = ev.get("area", "")
    if not a:
        return "before any area"
    n = AREA_TITLES.get(a, a)
    s = ev.get("section", "")
    return "%s, section %s" % (n, s) if s else n


def report(session, out_path):
    session = Path(session)
    out_dir = Path(out_path).parent
    events, problems = read_events(session / "events.jsonl")
    visits, info = build(events)
    t_first = info["t_first"] or 0.0
    total = (info["t_last"] or 0.0) - t_first
    allv = lambda attr: [e for v in visits for e in getattr(v, attr)]  # noqa: E731
    crashes, marks, saves, shots, slow = allv("crashes"), allv("marks"), allv("saves"), allv("shots"), allv("slow")
    areas = []
    for v in visits:
        if v.area and (AREA_TITLES.get(v.area, v.area)) not in areas:
            areas.append(AREA_TITLES.get(v.area, v.area))
    start = info["start"] or {}

    L = ["# Play session %s" % session.name, ""]
    if start.get("wall"):
        L += ["Started at %s (clock on the PC). Times below are from the start of the recording." % start["wall"], ""]

    for c in crashes:
        L += ["> **The game crashed** at %s in **%s**, position %s." % (clock(tnum(c) - t_first), where_text(c),
                                                                       crash_pos(c)),
              "> Details: %s access at guest address %s (rip %s)%s." % (
                  c.get("access", "?"), c.get("guest_va", "?"), c.get("rip", "?"),
                  "; the crash line was cut short" if c.get("torn") else ""),
              ""]

    L += ["## Summary", "",
          "| | |", "|---|---|",
          "| Play time | %s |" % clock(total),
          "| Areas seen | %s |" % (", ".join(areas) if areas else "none"),
          "| Area/section visits | %d |" % sum(1 for v in visits if v.area),
          "| Marks (F3) | %d |" % len(marks),
          "| Crashes | %s |" % (("**%d**" % len(crashes)) if crashes else "0"),
          "| Save copies | %d |" % len(saves),
          "| Screenshots | %d |" % len(shots),
          "| Slow frames (over %d ms) | %d |" % (SLOW_MS, len(slow)),
          ""]
    if problems:
        L += ["Reading notes: " + "; ".join(problems) + ".", ""]

    for n, v in enumerate(visits, 1):
        L += ["## %d. %s" % (n, v.name), ""]
        entry = clock(v.t0 - t_first) + (" (%s)" % v.wall if v.wall else "")
        L += ["- Entered at %s, stayed %s" % (entry, clock(v.t1 - v.t0))]
        for c in v.crashes:
            L += ["- **Crashed here** at %s, position %s" % (clock(tnum(c) - t_first), crash_pos(c))]
        if v.slow:
            worst = max(v.slow, key=lambda e: float(e.get("ms", 0)))
            L += ["- Slow frames: %d, worst %.0f ms at %s (position %s)" % (
                len(v.slow), float(worst.get("ms", 0)), clock(tnum(worst) - t_first), pos(worst))]
        else:
            L += ["- Slow frames: none"]
        if v.fps:
            rates = [float(e.get("fps", 0)) for e in v.fps]
            L += ["- Frame rate per minute: lowest %.1f, average %.1f" % (min(rates), sum(rates) / len(rates)), "",
                  "  | Minute ending | FPS | Worst frame | Frames over 100 ms |",
                  "  |---|---|---|---|"]
            for e in v.fps:
                L += ["  | %s | %.1f | %.0f ms | %d |" % (clock(tnum(e) - t_first), float(e.get("fps", 0)),
                                                       float(e.get("worst_ms", 0)), int(e.get("over100", 0)))]
            L += [""]
        if v.marks:
            L += ["", "Marks:", ""]
            for m in v.marks:
                s = " [screenshot](%s)" % link(session, out_dir, m["shot"]) if m.get("shot") else ""
                L += ["- %s at position %s%s" % (clock(tnum(m) - t_first), pos(m), s)]
        if v.shots:
            L += ["", "Screenshots:", ""]
            for s in v.shots:
                f = s.get("file", "")
                L += ["- %s %s: [%s](%s)" % (clock(tnum(s) - t_first), s.get("why", ""),
                                             Path(f.replace("\\", "/")).name, link(session, out_dir, f))]
        if v.saves:
            L += ["", "Save copies:", ""]
            for s in v.saves:
                c = s.get("copy", "")
                L += ["- %s: [%s](%s)" % (clock(tnum(s) - t_first), Path(c.replace("\\", "/")).name,
                                          link(session, out_dir, c))]
        L += [""]
    if not visits:
        L += ["No events were recorded.", ""]
    return re.sub(r"\n{3,}", "\n\n", "\n".join(L))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Write a Markdown report from one play-recorder session folder.")
    ap.add_argument("session", help="the session folder (has events.jsonl)")
    ap.add_argument("--out", help="report file (default reports/play/<session>.md)")
    a = ap.parse_args(argv)
    session = Path(a.session)
    if not session.is_dir():
        print("not a folder: %s" % session, file=sys.stderr)
        return 2
    out = Path(a.out) if a.out else Path("reports") / "play" / (session.resolve().name + ".md")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(report(session, out), encoding="utf-8")
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
