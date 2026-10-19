"""Guard the port lifecycle adapters against accidental retail logic changes."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class EntityLifecycleSource(unittest.TestCase):
    def test_damage_path_preserves_native_statements_when_no_reaction_is_pending(self):
        native = (ROOT / "ps2/src/monstorunit.cpp").read_text()
        port = (ROOT / "port/src/mods/entity_damage.cpp").read_text()
        signature = "int CMonstorUnit::CheckDmg()"
        hook = ("    const int scripted = mods::ConsumeEntityDamage(this, current_monster);\n"
                "    if (scripted != MONSTER_DMG_NONE) return scripted;\n")
        adapter = function(port, signature)
        self.assertEqual(adapter.count(hook), 1)
        self.assertEqual(adapter.replace(hook, ""), function(native, signature))

    def test_native_statements_are_preserved(self):
        native = (ROOT / "ps2/src/monstorunit.cpp").read_text()
        port = (ROOT / "port/src/mods/entity_lifecycle.cpp").read_text()
        cases = [
            ("void CMonstorUnit::CleanViewMonstor(int back_floor)",
             "    mods::EntityUnitCleared(this);\n"),
            ("int CMonstorUnit::SetupViewMonstor(int model_no, float *position, int event_flag)",
             "    mods::EntitySlotConstructed(this, current_monster);\n"),
        ]
        for signature, notification in cases:
            with self.subTest(signature=signature):
                adapter = function(port, signature)
                self.assertEqual(adapter.count(notification), 1)
                self.assertEqual(adapter.replace(notification, ""), function(native, signature))


if __name__ == "__main__":
    unittest.main()
