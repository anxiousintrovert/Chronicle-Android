import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('chronicle_mesh',Path(__file__).parents[1]/'chronicle_mesh.py')
mesh=importlib.util.module_from_spec(spec);spec.loader.exec_module(mesh)

class MeshAuthoring(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.source=self.root/'source.obj'
        self.base='v 0 0 0\nv 1 0 0\nv 0 1 0\n'
    def read(self,text):
        self.source.write_text(text,encoding='utf-8');return mesh.from_obj(self.source)
    def test_normals_uv_negative_indices_and_strip_groups(self):
        source=self.base+'vt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 2\nusemtl strip7\nf -3/1/1 -2/2/1 -1/3/1\n'
        result=self.read(source)
        self.assertEqual(result['triangles'],[[7,0,1,2]])
        self.assertEqual(result['vertices'][1],[1,0,0,0,0,1,1,0])
        output=mesh.write_new(result,self.root/'triangle.mesh.json')
        report=mesh.inspect(output)
        self.assertEqual(report['strips'],[7]);self.assertEqual(report['bounds']['max'],[1,1,0])
        self.assertEqual(self.source.read_text(),source)
    def test_missing_attributes_generate_flat_normals_without_reordering_winding(self):
        result=self.read(self.base+'f 1 3 2\n')
        self.assertEqual(result['vertices'][0][3:],[0,0,-1,0,0])
        self.assertEqual(result['triangles'],[[0,0,1,2]])
    def test_seams_keep_distinct_vertices_and_repeated_faces_reuse_them(self):
        result=self.read(self.base+'vt 0 0\nvt 1 1\nf 1/1 2/1 3/1\nf 1/2 2/1 3/1\nf 1/1 2/1 3/1\n')
        self.assertEqual(len(result['vertices']),4)
        self.assertEqual(result['triangles'][0],result['triangles'][2])
        self.assertNotEqual(result['triangles'][0][1],result['triangles'][1][1])
    def test_rejects_bad_geometry_indices_and_unsupported_primitives(self):
        for suffix in ['f 0 2 3','f 1 2 4','f -4 -2 -1','f 1 1 2','f 1 2 3 1','l 1 2','usemtl arbitrary','usemtl strip256','vn 0 0 0','f 1/1 2/1 3/1','f 1/0/0 2 3']:
            with self.subTest(suffix=suffix),self.assertRaises(ValueError):self.read(self.base+suffix+'\n')
        for value in ['nan','inf','1000001']:
            with self.assertRaises(ValueError):self.read('v '+value+' 0 0\n'+self.base+'f 2 3 4\n')
        with self.assertRaises(ValueError):self.read('v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n')
    def test_does_not_follow_material_libraries(self):
        result=self.read('mtllib ../../never-open.mtl\n'+self.base+'f 1 2 3\n')
        self.assertEqual(len(result['triangles']),1)
    def test_validator_rejects_duplicate_fields_boolean_indices_and_nonunit_normals(self):
        result=self.read(self.base+'f 1 2 3\n')
        for mutate in [lambda m:m.update(extra=1),lambda m:m.update(schema_version=True),lambda m:m['triangles'][0].__setitem__(1,True),lambda m:m['vertices'][0].__setitem__(5,2),lambda m:m['vertices'][0].__setitem__(0,float('nan'))]:
            copy=json.loads(json.dumps(result));mutate(copy)
            with self.assertRaises(ValueError):mesh.validate(copy)
        path=self.root/'duplicate.json';path.write_text('{"schema_version":1,"schema_version":1,"vertices":[],"triangles":[]}')
        with self.assertRaisesRegex(ValueError,'duplicate'):mesh.inspect(path)
    def test_limits_are_enforced_before_publication(self):
        with patch.object(mesh,'MAX_VERTICES',2),self.assertRaises(ValueError):self.read(self.base+'f 1 2 3\n')
        self.source.write_bytes(b'x'*20)
        with patch.object(mesh,'MAX_BYTES',19),self.assertRaises(ValueError):mesh.from_obj(self.source)
    def test_existing_output_ps2_path_and_failed_publication_are_preserved(self):
        result=self.read(self.base+'f 1 2 3\n');output=self.root/'output.json';output.write_text('keep')
        with self.assertRaises(FileExistsError):mesh.write_new(result,output)
        self.assertEqual(output.read_text(),'keep')
        with self.assertRaisesRegex(ValueError,'PS2'):mesh.write_new(result,self.root/'ps2'/'output.json')
        with patch.object(mesh.os,'link',side_effect=OSError('publication failed')),self.assertRaises(OSError):mesh.write_new(result,self.root/'new.json')
        self.assertFalse((self.root/'new.json').exists());self.assertEqual(list(self.root.glob('.chronicle-mesh-*')),[])

if __name__=='__main__':unittest.main()
