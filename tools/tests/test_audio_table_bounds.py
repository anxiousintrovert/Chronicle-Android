"""Keep port-only bounds synchronized with read-only retail declarations."""
from pathlib import Path
import re
import unittest


class RetailSoundBounds(unittest.TestCase):
    def test_port_table_bounds_match_retail_definitions(self):
        root = Path(__file__).resolve().parents[2]
        retail = (root / 'ps2/src/snd.cpp').read_text(encoding='utf-8')
        port = (root / 'port/src/mods/retail_sound.cpp').read_text(encoding='utf-8')
        pattern = r'SND_SE_INFO ((?:cap|voice)\d+)\[(\d+)\]'
        expected = dict(re.findall(pattern, retail))
        actual = dict(re.findall(pattern, port))
        self.assertTrue(expected)
        self.assertEqual(actual, expected)
        for name in expected:
            self.assertIn(f'if (table == {name}) return {name};', port)


if __name__ == '__main__':
    unittest.main()
