"""Native copies retain every original statement apart from explicit tuning hooks."""
from pathlib import Path
import unittest

from test_entity_lifecycle import function

ROOT = Path(__file__).resolve().parents[2]


class PlayerTuningSource(unittest.TestCase):
    def test_native_copies(self):
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
                 ('    mods::ApplyPlayerMapReveal(BtEquipMap, BtEquipMasuisyou);\n', '')]),
        ]
        for native, port, signature, replacements in cases:
            with self.subTest(signature=signature):
                original = function((ROOT / native).read_text(), signature)
                adapted = function((ROOT / port).read_text(), signature)
                for hook, old in replacements:
                    self.assertEqual(adapted.count(hook), 1)
                    adapted = adapted.replace(hook, old)
                self.assertEqual(original, adapted)


if __name__ == '__main__':
    unittest.main()
