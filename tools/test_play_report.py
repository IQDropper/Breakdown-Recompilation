"""tools/play_report.py on a small made-up session folder (stdlib only).

    py -3 tools/test_play_report.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import play_report as pr  # noqa: E402

# Lines shaped exactly like src/play_record_host.c writes them; the last one is a crash line cut short.
EVENTS = r'''{"t":0.4,"wall":"20:15:02","ev":"start","folder":"C:\\x\\20261114-201502","saves":"C:\\x","mark_key":"F3"}
{"t":2.0,"wall":"20:15:04","ev":"slow","ms":180.0,"area":"","section":""}
{"t":10.0,"wall":"20:15:12","ev":"area","area":"hon_under","section":"A","title":"7F / Main Research Center","new_area":true}
{"t":13.0,"wall":"20:15:15","ev":"shot","why":"area","file":"shots\\0001_area.png","area":"hon_under","section":"A","x":1.0,"y":2.0,"z":3.0}
{"t":15.0,"wall":"20:15:17","ev":"pos","area":"hon_under","section":"A","x":1.0,"y":2.0,"z":3.0}
{"t":40.0,"wall":"20:15:42","ev":"slow","ms":140.5,"area":"hon_under","section":"A","x":5.0,"y":2.0,"z":3.0}
{"t":45.0,"wall":"20:15:47","ev":"slow","ms":320.0,"area":"hon_under","section":"A","x":6.0,"y":2.0,"z":3.0}
{"t":60.0,"wall":"20:16:02","ev":"fps","fps":58.2,"frames":3492,"over50":3,"over100":2,"worst_ms":320.0}
{"t":70.0,"wall":"20:16:12","ev":"shot","why":"mark","file":"shots\\0002_mark.png","area":"hon_under","section":"A","x":7.0,"y":2.0,"z":3.0}
{"t":70.0,"wall":"20:16:12","ev":"mark","area":"hon_under","section":"A","x":7.0,"y":2.0,"z":3.0}
{"t":90.0,"wall":"20:16:32","ev":"save","copy":"saves\\201632_slot1","area":"hon_under","section":"A","x":7.0,"y":2.0,"z":3.0}
{"t":120.0,"wall":"20:17:02","ev":"fps","fps":60.0,"frames":3600,"over50":0,"over100":0,"worst_ms":20.0}
{"t":130.0,"wall":"20:17:12","ev":"area","area":"hon_under","section":"B","title":"7F / Main Research Center","new_area":false}
{"t":180.0,"wall":"20:18:02","ev":"fps","fps":44.0,"frames":2640,"over50":10,"over100":0,"worst_ms":70.0}
{"t":200.0,"wall":"20:18:22","ev":"area","area":"bettou","section":"","title":"Annex","new_area":true}
{"t":240.0,"wall":"20:19:02","ev":"fps","fps":30.0,"frames":1800,"over50":0,"over100":0,"worst_ms":40.0}
{"t":250.5,"ev":"crash","rip":"0x7FF6","fault":"0x10","guest_va":"0x0009FC60","access":"read","area":"bettou","section":"","x":9.0,"y'''


class PlayReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.session = self.root / "sessions" / "20261114-201502"
        (self.session / "shots").mkdir(parents=True)
        (self.session / "saves" / "201632_slot1").mkdir(parents=True)
        (self.session / "events.jsonl").write_text(EVENTS + "\n", encoding="utf-8")
        self.out = self.root / "reports" / "play" / "s.md"
        self.assertEqual(pr.main([str(self.session), "--out", str(self.out)]), 0)
        self.md = self.out.read_text(encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def section(self, title_start):
        parts = self.md.split("\n## ")
        hits = [p for p in parts if p.split("\n", 1)[0].split(". ", 1)[-1].startswith(title_start)]
        self.assertEqual(len(hits), 1, title_start)
        return hits[0]

    def test_torn_crash_line_is_kept_and_shown_first(self):
        top = self.md.split("## Summary")[0]
        self.assertIn("**The game crashed** at 4:10 in **Annex**", top)
        self.assertIn("cut short", top)
        self.assertIn("| Crashes | **1** |", self.md)

    def test_summary(self):
        self.assertIn("| Play time | 4:10 |", self.md)
        self.assertIn("| Areas seen | 7F / Main Research Center, Annex |", self.md)
        self.assertIn("| Area/section visits | 3 |", self.md)
        self.assertIn("| Marks (F3) | 1 |", self.md)
        self.assertIn("| Save copies | 1 |", self.md)
        self.assertIn("| Slow frames (over 100 ms) | 3 |", self.md)

    def test_sections_in_visit_order(self):
        titles = [l for l in self.md.splitlines() if l.startswith("## ") and l != "## Summary"]
        self.assertEqual(titles, ["## 1. Before the first area (menus, loading)",
                                  "## 2. 7F / Main Research Center, section A",
                                  "## 3. 7F / Main Research Center, section B",
                                  "## 4. Annex"])

    def test_area_details(self):
        a = self.section("7F / Main Research Center, section A")
        self.assertIn("Entered at 0:10 (20:15:12), stayed 2:00", a)
        self.assertIn("Slow frames: 2, worst 320 ms at 0:45 (position 6.0, 2.0, 3.0)", a)
        self.assertIn("lowest 58.2, average 59.1", a)
        self.assertIn("- 1:10 at position 7.0, 2.0, 3.0 [screenshot](../../sessions/20261114-201502/shots/0002_mark.png)", a)
        self.assertIn("[0001_area.png](../../sessions/20261114-201502/shots/0001_area.png)", a)
        self.assertIn("[201632_slot1](../../sessions/20261114-201502/saves/201632_slot1)", a)
        b = self.section("7F / Main Research Center, section B")
        self.assertIn("lowest 44.0, average 44.0", b)
        self.assertIn("Slow frames: none", b)
        self.assertIn("**Crashed here** at 4:10, position unknown (last known: 7.0, 2.0, 3.0)", self.section("Annex"))

    def test_links_point_at_real_files(self):
        target = self.out.parent / "../../sessions/20261114-201502/saves/201632_slot1"
        self.assertTrue(target.resolve().is_dir())

    def test_empty_and_missing(self):
        empty = self.root / "empty"
        empty.mkdir()
        md = pr.report(empty, self.root / "e.md")
        self.assertIn("No events were recorded.", md)
        self.assertIn("no events.jsonl", md)


if __name__ == "__main__":
    unittest.main()
