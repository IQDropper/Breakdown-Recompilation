"""Discovery, dump check, and wizard behavior for the first-run flow.

The search is limited to the repo root and build-win. A typed path is the
fallback when those two folders have no disc image.
"""

import io
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import first_run  # noqa: E402


def make_xbe(title_id):
    base = 0x00010000
    cert = base + 0x200
    data = bytearray(0x300)
    data[0:4] = b"XBEH"
    struct.pack_into("<I", data, 0x104, base)
    struct.pack_into("<I", data, 0x118, cert)
    struct.pack_into("<I", data, cert - base + 8, title_id)
    return bytes(data)


class DiscoverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        (self.repo / "build-win").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_breakdown_iso_is_found_and_unrelated_iso_is_not(self):
        usa = self.repo / "Breakdown (USA).iso"
        usa.write_bytes(b"disc")
        (self.repo / "build-win" / "other.iso").write_bytes(b"disc")
        (self.repo / "readme.txt").write_text("notes", encoding="utf-8")
        self.assertEqual(first_run.discover(self.repo), [usa])

    def test_finds_xiso_and_xiso_iso(self):
        xiso = self.repo / "owned.xiso"
        wrapped = self.repo / "owned.xiso.iso"
        xiso.write_bytes(b"a")
        wrapped.write_bytes(b"b")
        self.assertEqual(first_run.discover(self.repo), [xiso, wrapped])

    def test_named_image_in_build_win_beats_generic_iso_at_root(self):
        generic = self.repo / "random.iso"
        named = self.repo / "build-win" / "Breakdown (Europe).iso"
        generic.write_bytes(b"a")
        named.write_bytes(b"b")
        self.assertEqual(first_run.discover(self.repo)[0], named)

    def test_same_pattern_prefers_the_exe_folder(self):
        root_iso = self.repo / "Breakdown (Europe).iso"
        side_iso = self.repo / "build-win" / "Breakdown (Japan).iso"
        root_iso.write_bytes(b"a")
        side_iso.write_bytes(b"b")
        found = first_run.discover(self.repo)
        self.assertEqual(found[0], side_iso)
        self.assertEqual(found[1], root_iso)

    def test_breakdown_iso_beats_a_xiso_beside_the_exe(self):
        root_iso = self.repo / "Breakdown (USA).iso"
        side_xiso = self.repo / "build-win" / "owned.xiso"
        root_iso.write_bytes(b"a")
        side_xiso.write_bytes(b"b")
        found = first_run.discover(self.repo)
        self.assertEqual(found, [root_iso, side_xiso])

    def test_usa_name_leads_other_breakdown_isos_in_the_same_folder(self):
        europe = self.repo / "build-win" / "Breakdown (Europe).iso"
        usa = self.repo / "build-win" / "Breakdown (USA).iso"
        europe.write_bytes(b"a")
        usa.write_bytes(b"b")
        self.assertEqual(first_run.discover(self.repo)[0], usa)

    def test_case_insensitive_usa_name(self):
        usa = self.repo / "BREAKDOWN (USA).ISO"
        usa.write_bytes(b"a")
        self.assertEqual(first_run.discover(self.repo), [usa])

    def test_ignores_nested_folders_and_non_images(self):
        nested = self.repo / "isos"
        nested.mkdir()
        (nested / "Breakdown (USA).iso").write_bytes(b"a")
        (self.repo / "docs").mkdir()
        (self.repo / "docs" / "game.xiso").write_bytes(b"a")
        (self.repo / "default.xbe").write_bytes(b"a")
        (self.repo / "notes.md").write_text("x", encoding="utf-8")
        self.assertEqual(first_run.discover(self.repo), [])

    def test_ignores_directory_with_an_image_name(self):
        (self.repo / "Breakdown (USA).iso").mkdir()
        self.assertEqual(first_run.discover(self.repo), [])

    def test_missing_build_win_still_searches_root(self):
        (self.repo / "build-win").rmdir()
        iso = self.repo / "Breakdown (USA).iso"
        iso.write_bytes(b"a")
        self.assertEqual(first_run.discover(self.repo), [iso])


class ChooseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.lines = io.StringIO()

    def tearDown(self):
        self.tmp.cleanup()

    def choose(self, answers):
        pending = iter(answers)

        def ask(_prompt):
            return next(pending)

        return first_run.choose_iso(self.repo, input_fn=ask, output_fn=self.lines.write)

    def test_enter_accepts_suggestion(self):
        iso = self.repo / "Breakdown (USA).iso"
        iso.write_bytes(b"disc")
        self.assertEqual(self.choose([""]), iso)
        text = self.lines.getvalue()
        self.assertIn("Press Enter", text)
        self.assertIn("Breakdown (USA).iso", text)
        self.assertIn("next to FIRST_RUN.cmd", text)

    def test_number_picks_another_listed_image(self):
        first = self.repo / "Breakdown (USA).iso"
        (self.repo / "build-win").mkdir()
        second = self.repo / "build-win" / "extra.xiso"
        first.write_bytes(b"a")
        second.write_bytes(b"b")
        self.assertEqual(self.choose(["2"]), second)

    def test_empty_enter_rescans_until_a_drop_appears(self):
        answers = iter(["", ""])
        seen = {"n": 0}

        def ask(_prompt):
            seen["n"] += 1
            if seen["n"] == 1:
                self.assertIn("build-win", self.lines.getvalue())
                (self.repo / "owned.xiso").write_bytes(b"disc")
            return next(answers)

        picked = first_run.choose_iso(self.repo, input_fn=ask, output_fn=self.lines.write)
        self.assertEqual(picked, self.repo / "owned.xiso")

    def test_pasted_path_is_the_fallback(self):
        outside = self.repo / "elsewhere"
        outside.mkdir()
        iso = outside / "My Breakdown.iso"
        iso.write_bytes(b"disc")
        text_before = []

        def ask(_prompt):
            text_before.append(self.lines.getvalue())
            return str(iso)

        self.assertEqual(self.choose_with(ask), iso)
        self.assertIn("paste", text_before[0].lower())

    def choose_with(self, ask):
        return first_run.choose_iso(self.repo, input_fn=ask, output_fn=self.lines.write)

    def test_quoted_paste_and_folder_dump(self):
        folder = self.repo / "already"
        folder.mkdir()
        (folder / "default.xbe").write_bytes(b"xbe")
        self.assertEqual(self.choose(['"%s"' % folder]), folder)

    def test_missing_paste_asks_again(self):
        iso = self.repo / "Breakdown (USA).iso"
        answers = iter([str(self.repo / "nope.iso"), ""])

        def ask(_prompt):
            line = next(answers)
            if line == "":
                iso.write_bytes(b"disc")
            return line

        self.assertEqual(
            first_run.choose_iso(self.repo, input_fn=ask, output_fn=self.lines.write),
            iso,
        )
        self.assertIn("Not found", self.lines.getvalue())

    def test_q_quits(self):
        self.assertIsNone(self.choose(["q"]))


class DumpCheckTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        (self.repo / "game.toml").write_text(
            "\n".join(
                [
                    "[xbe]",
                    'path = "game_files/default.xbe"',
                    "title_id = 0x4E4D0009",
                    "[data]",
                    'game_files = "game_files"',
                    "",
                ]
            ),
            encoding="utf-8",
        )

    def tearDown(self):
        self.tmp.cleanup()

    def plant(self, title_id=0x4E4D0009, dirs=("romdata", "stream")):
        gf = self.repo / "game_files"
        gf.mkdir()
        (gf / "default.xbe").write_bytes(make_xbe(title_id))
        for name in dirs:
            (gf / name).mkdir()

    def test_pass_when_title_and_data_dirs_match(self):
        self.plant()
        rows = first_run.check_dump(self.repo)
        self.assertTrue(all(status == "pass" for status, _msg in rows))
        self.assertTrue(first_run.dump_ok(rows))

    def test_data_dirs_are_case_insensitive(self):
        self.plant(dirs=("RomData", "STREAM"))
        self.assertTrue(first_run.dump_ok(first_run.check_dump(self.repo)))

    def test_wrong_title_fails(self):
        self.plant(title_id=0x12345678)
        rows = first_run.check_dump(self.repo)
        self.assertFalse(first_run.dump_ok(rows))
        self.assertIn("0x4E4D0009", " ".join(msg for _s, msg in rows))

    def test_missing_stream_fails(self):
        self.plant(dirs=("romdata",))
        self.assertFalse(first_run.dump_ok(first_run.check_dump(self.repo)))

    def test_missing_xbe_fails(self):
        rows = first_run.check_dump(self.repo)
        self.assertFalse(first_run.dump_ok(rows))

    def test_real_game_toml_title(self):
        manifest = first_run.read_manifest(ROOT)
        self.assertEqual(manifest["game_files"], "game_files")
        self.assertEqual(manifest["xbe"], "game_files/default.xbe")
        self.assertEqual(manifest["title_id"], 0x4E4D0009)


class LaunchTests(unittest.TestCase):
    def test_xbe_loaded_is_pass(self):
        status, _msg = first_run.interpret_launch("XBE loaded: 12 bytes\n", "", 1, True)
        self.assertEqual(status, "pass")

    def test_cannot_open_xbe_is_fail_even_if_still_running(self):
        status, _msg = first_run.interpret_launch("", "Cannot open XBE: game_files/default.xbe\n", None, True)
        self.assertEqual(status, "fail")

    def test_timeout_without_banner_is_fail(self):
        status, _msg = first_run.interpret_launch("", "", None, True)
        self.assertEqual(status, "fail")

    def test_smoke_skips_when_exe_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            status, message = first_run.smoke(Path(tmp))
        self.assertEqual(status, "skip")
        self.assertIn("breakdown all", message)

    def test_smoke_kills_a_running_exe_after_the_banner(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            exe_dir = repo / "build-win"
            exe_dir.mkdir()
            exe = exe_dir / "breakdown_recomp.exe"
            exe.write_bytes(b"MZ")
            killed = {}

            class Proc:
                returncode = None

                def communicate(self, timeout=None):
                    if timeout is not None:
                        raise subprocess.TimeoutExpired("exe", timeout)
                    return b"XBE loaded: 4 bytes\n", b""

                def kill(self):
                    killed["yes"] = True

            calls = []

            def popen(cmd, **kwargs):
                calls.append((cmd, kwargs))
                return Proc()

            status, _msg = first_run.smoke(repo, seconds=8, popen=popen, platform_name="win32")
        self.assertEqual(status, "pass")
        self.assertTrue(killed.get("yes"))
        self.assertEqual(calls[0][0][0], str(exe))
        self.assertEqual(calls[0][1]["cwd"], str(repo))


class WizardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        (self.repo / "game.toml").write_text(
            '[xbe]\npath = "game_files/default.xbe"\ntitle_id = 0x4E4D0009\n'
            '[data]\ngame_files = "game_files"\n',
            encoding="utf-8",
        )
        self.iso = self.repo / "Breakdown (USA).iso"
        self.iso.write_bytes(b"disc")
        gf = self.repo / "game_files"
        gf.mkdir()
        (gf / "default.xbe").write_bytes(make_xbe(0x4E4D0009))
        (gf / "romdata").mkdir()
        (gf / "stream").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_wizard_imports_then_doctor_and_skips_missing_exe(self):
        calls = []

        def runner(cmd, cwd):
            calls.append((cmd, cwd))
            return subprocess.CompletedProcess(cmd, 0)

        code = first_run.wizard(self.repo, self.iso, runner=runner)
        self.assertEqual(code, 0)
        self.assertEqual(calls[0][0][-2:], ["import", str(self.iso)])
        self.assertEqual(calls[1][0][-1], "doctor")
        self.assertEqual(calls[0][1], str(self.repo))

    def test_import_failure_fails_the_wizard(self):
        def runner(cmd, cwd):
            code = 1 if "import" in cmd else 0
            return subprocess.CompletedProcess(cmd, code)

        self.assertEqual(first_run.wizard(self.repo, self.iso, runner=runner), 1)

    def test_doctor_failure_fails_the_wizard(self):
        def runner(cmd, cwd):
            code = 1 if cmd[-1] == "doctor" else 0
            return subprocess.CompletedProcess(cmd, code)

        self.assertEqual(first_run.wizard(self.repo, self.iso, runner=runner), 1)


class ScriptContractTests(unittest.TestCase):
    def test_first_run_cmd_uses_in_repo_choice_then_wizard(self):
        text = (ROOT / "FIRST_RUN.cmd").read_text(encoding="utf-8")
        self.assertIn("first_run.py choose", text)
        self.assertIn("first_run.py wizard", text)
        self.assertIn("SETUP.cmd", text)
        self.assertIn("nopause", text)
        self.assertNotIn("OpenFileDialog", text)
        self.assertNotIn("FileDialog", text)

    def test_setup_can_return_without_pause(self):
        text = (ROOT / "SETUP.cmd").read_text(encoding="utf-8")
        self.assertIn("nopause", text)
        self.assertIn("FIRST_RUN.cmd", text)

    def test_readme_and_doc_describe_the_two_drop_folders(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        doc = (ROOT / "docs" / "first-run.md").read_text(encoding="utf-8")
        for text in (readme, doc):
            self.assertIn("FIRST_RUN.cmd", text)
            self.assertIn("build-win", text)
            self.assertIn("Breakdown (USA).iso", text)
        self.assertIn("breakdown all", doc)
        self.assertIn("uv", doc.lower())
        self.assertIn("own", doc.lower())


if __name__ == "__main__":
    unittest.main()
