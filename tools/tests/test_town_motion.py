"""Keep both motion adapters identical to retail apart from scoped resolution."""
from pathlib import Path
import unittest

from test_entity_lifecycle import function

ROOT = Path(__file__).resolve().parents[2]


class TownMotionSource(unittest.TestCase):
    def test_native_body_loader_only_adds_retirement_notification(self):
        native = (ROOT / "ps2/src/editloop.cpp").read_text()
        port = (ROOT / "port/src/mods/town_native.cpp").read_text()
        signature = "void EdLoadMainChara("
        hook = "    mods::TownBodyLoaded();\n"
        adapter = function(port, signature)
        self.assertEqual(adapter.count(hook), 1)
        self.assertEqual(adapter.replace(hook, ""), function(native, signature))

    def test_model_and_shadow_preserve_native_statements(self):
        native = (ROOT / "ps2/src/character.cpp").read_text()
        port = (ROOT / "port/src/mods/town_motion_native.cpp").read_text()
        hook = "    motion_no = mods::ResolveTownMotion(this, motion_no);\n"
        for signature in ("tagMOTION_TYPE *CCharacter::GetMotionParam(",
                          "void CCharacter::ShadowStep()"):
            with self.subTest(signature=signature):
                adapter = function(port, signature)
                self.assertEqual(adapter.count(hook), 1)
                self.assertEqual(adapter.replace(hook, ""), function(native, signature))


if __name__ == "__main__":
    unittest.main()
