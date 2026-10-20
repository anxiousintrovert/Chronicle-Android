import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
spec=importlib.util.spec_from_file_location('chronicle_texture',Path(__file__).parents[1]/'chronicle_texture.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
class TextureTools(unittest.TestCase):
    def test_snapshot_export_preserves_alpha_digest_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);pixels=bytes([255,0,0,255,0,255,0,0]);digest=hashlib.sha256(b'chronicle-rgba-v1:2:1:'+pixels).hexdigest()
            info={'schema_version':1,'name':'atlas','width':2,'height':1,'original_sha256':digest,'encoding':'rgba8'}
            snapshot=root/'original.texture';snapshot.write_bytes(json.dumps(info).encode()+b'\n'+pixels)
            declaration=tool.export_png(snapshot,root/'image.png');self.assertEqual(declaration,{'name':'atlas','original_sha256':digest,'source':'image.png'})
            png=(root/'image.png').read_bytes();length=struct.unpack('>I',png[33:37])[0]
            self.assertEqual(zlib.decompress(png[41:41+length]),b'\x00'+pixels)
            with self.assertRaises(FileExistsError):tool.export_png(snapshot,root/'image.png')
            with self.assertRaises(ValueError):tool.export_png(snapshot,root/'ps2'/'image.png')
            self.assertFalse((root/'ps2'/'image.png').exists())
            snapshot.write_bytes(json.dumps(info).encode()+b'\n'+pixels[:-1])
            with self.assertRaises(ValueError):tool.read_snapshot(snapshot)
            snapshot.write_bytes(json.dumps(info).encode()+b'\n'+bytes(8))
            with self.assertRaises(ValueError):tool.read_snapshot(snapshot)
    def test_package_keeps_png_pixels_target_and_default_disabled_install(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);pixels=bytes([25,100,200,255])*2
            info={'schema_version':1,'name':'atlas','width':2,'height':1,
                  'original_sha256':hashlib.sha256(b'chronicle-rgba-v1:2:1:'+pixels).hexdigest(),'encoding':'rgba8'}
            snapshot=root/'original.texture';snapshot.write_bytes(json.dumps(info).encode()+b'\n'+pixels)
            tool.export_png(snapshot,root/'image.png');png=(root/'image.png').read_bytes()
            report=tool.create_package(snapshot,root/'image.png','test.texture',root/'package')
            self.assertTrue(report['profile_unchanged']);self.assertEqual(report['target']['original_sha256'],info['original_sha256'])
            self.assertEqual((root/'package/replacement.png').read_bytes(),png)
            manifest=json.loads((root/'package/mod.json').read_text());self.assertNotIn('enabled',manifest);self.assertNotIn('script',manifest)
            spec=importlib.util.spec_from_file_location('chronicle_mod',Path(tool.__file__).with_name('chronicle_mod.py'))
            author=importlib.util.module_from_spec(spec);spec.loader.exec_module(author)
            self.assertEqual(author.validate(root/'package'),manifest)
            author.pack(root/'package',root/'texture.zip');author.install(root/'texture.zip',root/'mods')
            self.assertEqual((root/'mods/test.texture/replacement.png').read_bytes(),png)
            self.assertFalse((root/'mods-profile.json').exists())
            with self.assertRaises(FileExistsError):tool.create_package(snapshot,root/'image.png','test.texture',root/'package')
            with self.assertRaises(ValueError):tool.create_package(snapshot,root/'image.png','test.texture',root/'PS2'/'package')
            self.assertFalse((root/'PS2').exists())
            with self.assertRaises(ValueError):tool.create_package(snapshot,root/'image.png','Invalid ID',root/'invalid-id')
            self.assertFalse((root/'invalid-id').exists())
            (root/'image.png').write_bytes(b'bad')
            with self.assertRaises(ValueError):tool.create_package(snapshot,root/'image.png','test.texture',root/'invalid-image')
            self.assertFalse((root/'invalid-image').exists())
if __name__=='__main__':unittest.main()
