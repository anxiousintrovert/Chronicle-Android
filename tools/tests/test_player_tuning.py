"""Port copies retain checked native statements apart from explicit tuning hooks."""
import json
from pathlib import Path
import unittest

from test_entity_lifecycle import function
from test_native_overlay import overlay

ROOT = Path(__file__).resolve().parents[2]


class PlayerTuningSource(unittest.TestCase):
    def test_native_copies(self):
        native_sources = overlay.prepare(ROOT / 'ps2', json.loads((ROOT / 'port/native_overlay.json').read_text()))
        cases = [
            ('ps2/src/dngstatusdata.cpp', 'port/src/mods/player_thirst_native.cpp',
             'void CUserStatus::Step(', [
                 ('                mods::ScalePlayerValue(mods::PlayerTuning::ThirstDrain, drain);\n', '')]),
            ('ps2/src/hitvalue.cpp', 'port/src/mods/player_wear_native.cpp',
             'int BattleSubWeaponDmg(', [
                 ('    mods::ScalePlayerValue(mods::PlayerTuning::DurabilityWear, wear);\n', '')]),
            ('ps2/src/dun/gameloop.cpp', 'port/src/dun/gameloop.cpp',
             'void motionDrive()', [
                 ('        mods::StepPlayerMotion();', '        CharaMain.Step();'),
                 ('        mods::ScalePlayerValue(mods::PlayerTuning::GaugeRefill, gain);\n', '')]),
            ('ps2/src/dungeonmap.cpp', 'port/src/dungeonmap.cpp',
             'void CDungeonMap::DrawMiniMap(', [
                 ('    if (this->GridExtent() == chronicle::dungeon::GridStride) {\n'
                  '        DrawExpandedMiniMap(*this, pos, angle);\n'
                  '        return;\n'
                  '    }\n', ''),
                 ('    mods::ApplyPlayerMapReveal(BtEquipMap, BtEquipMasuisyou);\n', '')]),
        ]
        for native, port, signature, replacements in cases:
            with self.subTest(signature=signature):
                relative = Path(native).relative_to('ps2').as_posix()
                original = function(native_sources[relative].decode('utf-8'), signature)
                adapted = function((ROOT / port).read_text(), signature)
                for hook, old in replacements:
                    self.assertEqual(adapted.count(hook), 1)
                    adapted = adapted.replace(hook, old)
                self.assertEqual(original, adapted)


if __name__ == '__main__':
    unittest.main()
