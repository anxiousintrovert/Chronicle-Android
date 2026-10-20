import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('chronicle_mesh_glb',Path(__file__).parents[1]/'chronicle_mesh.py')
mesh=importlib.util.module_from_spec(spec);spec.loader.exec_module(mesh)

class GlbAuthoring(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'input.glb'
        self.binary=struct.pack('<9f3H',0,0,0,1,0,0,0,1,0,0,1,2)
        self.doc={'asset':{'version':'2.0'},'buffers':[{'byteLength':42}],
            'bufferViews':[{'buffer':0,'byteLength':36},{'buffer':0,'byteOffset':36,'byteLength':6}],
            'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3'},
                         {'bufferView':1,'componentType':5123,'count':3,'type':'SCALAR'}],
            'meshes':[{'primitives':[{'attributes':{'POSITION':0},'indices':1,'material':0}]}],
            'materials':[{'name':'strip7'}]}
    def write(self):
        text=json.dumps(self.doc,separators=(',',':')).encode();text+=b' '*((-len(text))%4)
        binary=self.binary+b'\0'*((-len(self.binary))%4)
        raw=struct.pack('<III',0x46546c67,2,28+len(text)+len(binary))
        raw+=struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(binary),0x004e4942)+binary
        self.path.write_bytes(raw);return raw
    def read(self):
        self.write();return mesh.from_glb(self.path)
    def test_indexed_geometry_strip_winding_and_exclusive_publication(self):
        result=self.read();original=self.path.read_bytes()
        self.assertEqual(result['triangles'],[[7,0,1,2]])
        self.assertEqual(result['vertices'][1],[1,0,0,0,0,1,0,0])
        output=self.path.with_suffix('.mesh.json');mesh.write_new(result,output)
        self.assertEqual(mesh.inspect(output)['strips'],[7]);self.assertEqual(self.path.read_bytes(),original)
        with self.assertRaises(FileExistsError):mesh.write_new(result,output)
    def test_nonindexed_selection_and_scene_transforms_are_not_baked(self):
        primitive=self.doc['meshes'][0]['primitives'][0];primitive.pop('indices');primitive.pop('material')
        self.doc['meshes'].insert(0,{'primitives':[]})
        self.doc['nodes']=[{'mesh':1,'translation':[100,200,300]}]
        self.write();result=mesh.from_glb(self.path,1)
        self.assertEqual(result['vertices'][0][:3],[0,0,0]);self.assertEqual(result['triangles'][0][0],0)
        with self.assertRaises(ValueError):mesh.from_glb(self.path,2)
    def test_interleaved_attributes_and_normalized_uvs(self):
        self.binary=b''.join(struct.pack('<6f2H',*position,0,0,2,*uv) for position,uv in
                            [((0,0,0),(0,0)),((1,0,0),(65535,0)),((0,1,0),(0,65535))])
        self.doc['buffers'][0]['byteLength']=len(self.binary)
        self.doc['bufferViews']=[{'buffer':0,'byteLength':84,'byteStride':28}]
        self.doc['accessors']=[{'bufferView':0,'byteOffset':offset,'componentType':component,'count':3,'type':shape,**extra}
            for offset,component,shape,extra in [(0,5126,'VEC3',{}),(12,5126,'VEC3',{}),(24,5123,'VEC2',{'normalized':True})]]
        primitive=self.doc['meshes'][0]['primitives'][0];primitive.pop('indices')
        primitive['attributes']={'POSITION':0,'NORMAL':1,'TEXCOORD_0':2}
        result=self.read();self.assertEqual(result['vertices'][1],[1,0,0,0,0,1,1,0])
    def test_rejects_unsupported_features_and_bad_references(self):
        baseline=json.loads(json.dumps(self.doc))
        changes=[lambda d:d.update(animations=[{}]),lambda d:d.update(skins=[{}]),
            lambda d:d.update(extensionsUsed=['KHR_draco_mesh_compression']),
            lambda d:d['buffers'][0].update(uri='../../private.bin'),
            lambda d:d['accessors'][0].update(sparse={}),
            lambda d:d['accessors'][0].update(count=True),
            lambda d:d['accessors'][0].update(count=4),
            lambda d:d['accessors'][0].update(byteOffset=1),
            lambda d:d['accessors'][1].update(normalized=True),
            lambda d:d['bufferViews'][0].update(byteLength=35),
            lambda d:d['bufferViews'][0].update(byteStride=8),
            lambda d:d['materials'][0].update(name='body'),
            lambda d:d['meshes'][0]['primitives'][0].update(mode=5),
            lambda d:d['meshes'][0]['primitives'][0].update(targets=[]),
            lambda d:d['meshes'][0]['primitives'][0]['attributes'].update(JOINTS_0=0)]
        for change in changes:
            self.doc=json.loads(json.dumps(baseline));change(self.doc)
            with self.subTest(doc=self.doc),self.assertRaises(ValueError):self.read()
    def test_rejects_truncated_headers_chunks_and_invalid_indices(self):
        raw=self.write()
        for data in [raw[:10],raw[:-1],raw+b'\0',b'BAD!'+raw[4:],raw[:12]+struct.pack('<I',0xfffffffc)+raw[16:]]:
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):mesh.from_glb(self.path)
        self.binary=self.binary[:-2]+struct.pack('<H',99)
        with self.assertRaisesRegex(ValueError,'index'):self.read()
    def test_rejects_nonfinite_collapsed_geometry_and_enforces_budgets(self):
        original=self.binary
        for value in [float('nan'),float('inf'),1000001.0]:
            self.binary=struct.pack('<f',value)+original[4:]
            with self.assertRaises(ValueError):self.read()
        self.binary=original;self.write()
        with patch.object(mesh,'MAX_BYTES',20),self.assertRaises(ValueError):mesh.from_glb(self.path)
        with patch.object(mesh,'MAX_VERTICES',2),self.assertRaises(ValueError):mesh.from_glb(self.path)
        self.binary=struct.pack('<9f3H',0,0,0,1,0,0,2,0,0,0,1,2)
        with self.assertRaises(ValueError):self.read()

if __name__=='__main__':unittest.main()
