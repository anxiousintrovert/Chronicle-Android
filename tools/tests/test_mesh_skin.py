from pathlib import Path
import unittest
import hashlib
from test_entity_lifecycle import function
ROOT=Path(__file__).resolve().parents[2]

class MeshSkinSource(unittest.TestCase):
    def test_replacement_skin_kernel_matches_unchanged_native_port_kernel(self):
        signature='void SkinVertex('
        native=(ROOT/'port/src/gameutil_motion.cpp').read_text()
        adapter=(ROOT/'port/src/mods/mesh_skin.cpp').read_text()
        self.assertEqual(function(adapter,signature),function(native,signature))

    def test_native_motion_statements_remain_unchanged_around_notifications(self):
        native=(ROOT/'port/src/gameutil_motion.cpp').read_text()
        body=function(native,'Mot_List *MotionProc2(')
        for hook in ["        mods::BeginNativeMeshSkin(model, frame_info[list->frame].base_vertices, frame_info[list->frame].vertex_count);\n",
                     "    const bool mod_skin = mods::NativeMeshSkinActive();\n",
                     "        if (mod_skin) mods::ApplyNativeMeshBone(vertex, Bone_Matrix_Base, Bone_Matrix, Bone_Matrix_inv, 0.01f * list->keys[i].value[0]);\n"]:
            self.assertEqual(body.count(hook),1)
            body=body.replace(hook,"")
        self.assertEqual(hashlib.sha256(body.encode()).hexdigest(),"383ebc685f58f3d11ddf5140d00be20a494b43c883592cf4704dba04007c075c")

if __name__=='__main__':unittest.main()
