"""Synthetic inputs test metric integrity; they are not NieR progress."""

import copy
import importlib.util
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

spec = importlib.util.spec_from_file_location("progress", Path(__file__).resolve().parents[1] / "tools/progress.py")
progress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(progress)


class ProgressTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "proof.md").write_text("Synthetic evidence; not a game result.")
        (self.root / "source.cpp").write_text("int synthetic() { return 1; }")
        self.document = {
            "schema_version": 1,
            "baseline": {"version": "synthetic", "executable_sha256": "a" * 64,
                         "inventory_complete": True, "inventory_evidence": "proof.md",
                         "code_ranges": [{"address": 4096, "size": 1000}]},
            "functions": [
                {"address": 4096, "size": 100, "status": "verified", "source": "source.cpp", "evidence": ["proof.md"]},
                {"address": 4196, "size": 900, "status": "recompiled"},
            ],
        }

    def calculate(self):
        return progress.calculate(self.document, self.root)

    def test_byte_weighted_and_recompilation_separate(self):
        coverage = self.calculate()["coverage"]
        self.assertEqual(coverage["verified"]["percent"], 10)
        self.assertEqual(coverage["decompiled"]["percent"], 10)
        self.assertEqual(coverage["recompiled"]["percent"], 100)
        self.assertEqual(coverage["verified"]["functions"], 1)

    def test_partial_inventory_is_unmeasured(self):
        self.document["baseline"]["inventory_complete"] = False
        self.assertIsNone(self.calculate()["coverage"]["verified"]["percent"])

    def test_complete_inventory_needs_real_identity_and_review(self):
        for key, value in [("executable_sha256", None), ("inventory_evidence", None), ("version", None)]:
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                document["baseline"][key] = value
                with self.assertRaises(ValueError):
                    progress.calculate(document, self.root)

    def test_overlaps_and_outside_ranges_rejected(self):
        for address in [4096, 4100, 9000]:
            with self.subTest(address=address):
                self.document["functions"][1]["address"] = address
                with self.assertRaises(ValueError):
                    self.calculate()

    def test_verified_requires_source_and_evidence(self):
        for field, value in [("source", None), ("evidence", []), ("evidence", ["missing.md"]), ("source", "../escape.cpp")]:
            with self.subTest(field=field, value=value):
                document = copy.deepcopy(self.document)
                document["functions"][0][field] = value
                with self.assertRaises(ValueError):
                    progress.calculate(document, self.root)

    def test_baseline_changes_only_with_inventory_or_executable(self):
        before = self.calculate()["baseline_id"]
        self.document["functions"][1]["status"] = "discovered"
        self.assertEqual(before, self.calculate()["baseline_id"])
        self.document["functions"][1]["size"] = 800
        self.assertNotEqual(before, self.calculate()["baseline_id"])

    def test_snapshot_same_day_replaces_and_keeps_unmeasured(self):
        metrics = self.calculate()
        history = progress.snapshot([], metrics, "2026-11-06")
        self.document["baseline"]["inventory_complete"] = False
        history = progress.snapshot(history, self.calculate(), "2026-11-06")
        self.assertEqual(len(history), 1)
        self.assertIsNone(history[0]["verified_percent"])

    def test_bad_history_rejected(self):
        history = progress.snapshot([], self.calculate(), "2026-11-06")
        for value in [float("nan"), -1, 101, True]:
            history[0]["verified_percent"] = value
            with self.assertRaises(ValueError):
                progress.validate_history(history)

    def test_done_milestone_requires_evidence(self):
        with self.assertRaises(ValueError):
            progress.milestones({"schema_version": 1, "milestones": [
                {"id": "runtime", "label": "Synthetic", "status": "done", "evidence": None}
            ]}, self.root)

    def test_svg_escapes_labels_and_has_no_fake_zero_point(self):
        self.document["baseline"]["inventory_complete"] = False
        metrics = self.calculate()
        history = progress.snapshot([], metrics, "2026-11-06")
        generated = progress.outputs(metrics, [{"id": "test", "label": "<synthetic & test>", "status": "pending"}], history)
        for name, content in generated.items():
            if name.endswith(".svg"):
                ET.fromstring(content)
        self.assertIn("&lt;synthetic &amp; test&gt;", generated["docs/generated/progress.svg"])
        self.assertNotIn("<circle", generated["docs/generated/history.svg"])

    def test_history_does_not_join_different_baselines(self):
        history = progress.snapshot([], self.calculate(), "2026-11-05")
        self.document["baseline"]["executable_sha256"] = "b" * 64
        history = progress.snapshot(history, self.calculate(), "2026-11-06")
        self.assertNotIn('stroke="#34d399" stroke-width="3"', progress.history_svg(history))


if __name__ == "__main__":
    unittest.main()
