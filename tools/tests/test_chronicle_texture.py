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
if __name__=='__main__':unittest.main()
