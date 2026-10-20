import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("native_overlay", Path(__file__).parents[1] / "native_overlay.py")
overlay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(overlay)


class NativeOverlayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "native"
        self.output = self.root / "generated"
        (self.source / "include/sub").mkdir(parents=True)
        (self.source / "src").mkdir()
        self.header = self.source / "include/sub/layout.hpp"
        self.header.write_bytes(b"#pragma once\r\nstruct Layout { int cells[400]; };\r\n")
        (self.source / "src/layout.cpp").write_bytes(b'#include "sub/layout.hpp"\nLayout map;\n')
        self.manifest = {"schema_version": 1, "files": {}}

    def patch(self, old="cells[400]", new="cells[2304]", count=1):
        self.manifest["files"]["include/sub/layout.hpp"] = {
            "sha256": overlay.digest(overlay.canonical(self.header.read_bytes())),
            "replacements": [{"old": old, "new": new, "count": count}],
        }

    def run_overlay(self, **kwargs):
        return overlay.generate(self.source, self.output, self.manifest, **kwargs)

    def test_identity_mirror_preserves_bytes_and_unchanged_timestamps(self):
        self.assertEqual(self.run_overlay(), 2)
        for path in self.source.rglob("*"):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (self.output / path.relative_to(self.source)).read_bytes())
        result = self.output / "include/sub/layout.hpp"
        timestamp = result.stat().st_mtime_ns
        self.run_overlay()
        self.assertEqual(result.stat().st_mtime_ns, timestamp)

    def test_checked_patch_changes_copy_only_and_normalizes_line_endings(self):
        original = self.header.read_bytes()
        self.patch()
        self.run_overlay()
        self.assertIn(b"cells[2304]", (self.output / "include/sub/layout.hpp").read_bytes())
        self.assertEqual(self.header.read_bytes(), original)
        self.header.write_bytes(original.replace(b"\r\n", b"\n"))
        self.run_overlay()  # Checkout line endings do not invalidate the pin.

    def test_stale_pin_or_wrong_match_count_preserves_existing_outputs(self):
        self.run_overlay()
        before = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.patch(count=2)
        with self.assertRaisesRegex(overlay.OverlayError, "count mismatch"):
            self.run_overlay()
        self.patch()
        self.header.write_bytes(b"changed upstream\n")
        with self.assertRaisesRegex(overlay.OverlayError, "source changed"):
            self.run_overlay()
        after = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_check_mode_never_creates_output(self):
        self.patch()
        self.assertEqual(self.run_overlay(check=True), 2)
        self.assertFalse(self.output.exists())

    def test_protected_overlapping_and_traversing_paths_reject(self):
        for output in (self.source, self.source / "generated", self.root, self.root / "PS2/generated"):
            with self.assertRaises(overlay.OverlayError):
                overlay.generate(self.source, output, self.manifest)
        for path in ("../bad", "include/../bad", "/include/bad", "include\\bad", "src/x:y", "include//bad"):
            with self.assertRaises(overlay.OverlayError):
                overlay.relative_path(path)
        self.assertFalse((self.root / "PS2").exists())

    def test_stale_managed_files_removed_unmanaged_outputs_preserved(self):
        self.run_overlay()
        (self.output / "notes.txt").write_text("keep")
        (self.source / "src/layout.cpp").unlink()
        self.run_overlay()
        self.assertFalse((self.output / "src/layout.cpp").exists())
        self.assertEqual((self.output / "notes.txt").read_text(), "keep")
        (self.output / "include/new.hpp").write_text("unmanaged")
        (self.source / "include/new.hpp").write_text("new source")
        with self.assertRaisesRegex(overlay.OverlayError, "unmanaged"):
            self.run_overlay()
        self.assertEqual((self.output / "include/new.hpp").read_text(), "unmanaged")

    def test_invalid_ownership_index_cannot_remove_external_file(self):
        self.run_overlay()
        outside = self.root / "keep.txt"
        outside.write_text("keep")
        (self.output / "overlay-files.json").write_text(json.dumps(["../keep.txt"]))
        with self.assertRaises(overlay.OverlayError):
            self.run_overlay()
        self.assertEqual(outside.read_text(), "keep")

    def test_linked_input_or_output_rejects_before_changes(self):
        actual = overlay.linked
        with patch.object(overlay, "linked", side_effect=lambda p: p == self.header or actual(p)):
            with self.assertRaisesRegex(overlay.OverlayError, "linked native input"):
                self.run_overlay()
        self.assertFalse(self.output.exists())
        self.run_overlay()
        destination = self.output / "include/sub/layout.hpp"
        before = destination.read_bytes()
        self.patch()
        with patch.object(overlay, "linked", side_effect=lambda p: p == destination or actual(p)):
            with self.assertRaisesRegex(overlay.OverlayError, "linked overlay destination"):
                self.run_overlay()
        self.assertEqual(destination.read_bytes(), before)

    def test_invalid_manifest_cannot_start_generation(self):
        for manifest in ({}, {"schema_version": True, "files": {}},
                         {"schema_version": 1, "files": []},
                         {"schema_version": 1, "files": {}, "extra": 1}):
            with self.assertRaises(overlay.OverlayError):
                overlay.generate(self.source, self.output, manifest)
        self.patch(old="", count=0)
        with self.assertRaises(overlay.OverlayError):
            self.run_overlay()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
