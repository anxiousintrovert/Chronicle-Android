from pathlib import Path
import unittest
from test_entity_lifecycle import function
ROOT=Path(__file__).resolve().parents[2]

class MeshSkinSource(unittest.TestCase):
    def test_replacement_skin_kernel_matches_unchanged_native_port_kernel(self):
        signature='void SkinVertex('
        native=(ROOT/'port/src/gameutil_motion.cpp').read_text()
        adapter=(ROOT/'port/src/mods/mesh_skin.cpp').read_text()
        self.assertEqual(function(adapter,signature),function(native,signature))

if __name__=='__main__':unittest.main()
