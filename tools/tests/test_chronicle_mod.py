import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('chronicle_mod', Path(__file__).parents[1] / 'chronicle_mod.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class AuthorTools(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def package(self, name, **changes):
        folder = self.root / 'mods' / name
        tool.create(name, folder)
        manifest = folder / 'mod.json'
        data = json.loads(manifest.read_text())
        data.update(changes)
        manifest.write_text(json.dumps(data))
        return folder

    def test_starter_is_disabled_and_archive_contains_only_declared_files(self):
        folder = self.package('test.one')
        self.assertNotIn('enabled', tool.validate(folder))
        (folder / 'private.txt').write_text('do not ship')
        archive = self.root / 'package.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(set(packed.namelist()), {'test.one/mod.json', 'test.one/main.lua', 'test.one/README.md'})
        with self.assertRaises(FileExistsError):
            tool.pack(folder, archive)

    def test_path_escape_and_duplicate_json_rejected(self):
        folder = self.package('test.one', resources={'bad': '../outside.txt'})
        with self.assertRaises(ValueError):
            tool.validate(folder)
        (folder / 'mod.json').write_text('{"id":"test.one","id":"test.two"}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON key'):
            tool.validate(folder)

    def test_dependency_conflict_plan_never_enables_or_writes_profile(self):
        self.package('test.one', exclusive=['combat.formula'])
        self.package('test.two', exclusive=['combat.formula'])
        self.package('test.three', requires=['test.one'])
        empty = tool.check_set(self.root / 'mods', [])
        self.assertFalse(empty['problems'])
        result = tool.check_set(self.root / 'mods', ['test.one', 'test.two', 'test.three'])
        self.assertIn('exclusive feature', result['problems']['test.one'][0])
        self.assertIn('blocked dependency', result['problems']['test.three'][0])
        self.assertFalse(list(self.root.rglob('mods-profile.json')))

    def test_dependency_cycles_and_missing_ids_reported(self):
        self.package('test.one', requires=['test.two'])
        self.package('test.two', requires=['test.one'])
        result = tool.check_set(self.root / 'mods', ['test.one', 'test.two', 'test.missing'])
        self.assertIn('dependency cycle', result['problems']['test.one'])
        self.assertTrue(result['problems']['test.missing'])

    def test_native_is_packaged_but_never_trusted_by_cli(self):
        folder = self.package('test.one', native_library='plugin.dll', permissions=['native.extension'])
        (folder / 'plugin.dll').write_bytes(b'not executed')
        archive = self.root / 'package.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertIn('test.one/plugin.dll', packed.namelist())
        result = tool.check_set(self.root / 'mods', ['test.one'])
        self.assertIn('native trust', result['problems']['test.one'][0])

    def test_catalog_has_real_operations_and_does_not_claim_future_hooks(self):
        operations = {op['name']: op for op in tool.apis()}
        self.assertEqual(operations['player.heal']['permission'], 'game.write')
        self.assertTrue(operations['economy.spend']['writes'])
        self.assertNotIn('entities.spawn', operations)

    def test_required_operations_need_implementation_and_permission(self):
        missing = self.package('test.missing', requires_operations=['entities.spawn'])
        with self.assertRaisesRegex(ValueError, 'missing host operation'):
            tool.validate(missing)
        denied = self.package('test.denied', requires_operations=['story.flag_set'])
        with self.assertRaisesRegex(ValueError, 'needs permission'):
            tool.validate(denied)
        ready = self.package('test.ready', requires_operations=['story.flag_set'], permissions=['game.write'])
        self.assertEqual(tool.validate(ready)['id'], 'test.ready')

    def test_seed_comparison_rejects_rng_differences_and_truncation(self):
        baseline, candidate = self.root / 'baseline.jsonl', self.root / 'candidate.jsonl'
        baseline.write_bytes(b'{"seed":1,"next_rng":42}\n{"seed":2,"next_rng":19}\n')
        candidate.write_bytes(baseline.read_bytes())
        self.assertTrue(tool.compare_seeds(baseline, [candidate])['candidates'][0]['matches'])
        candidate.write_bytes(b'{"seed":1,"next_rng":43}\n{"seed":2,"next_rng":19}\n')
        mismatch = tool.compare_seeds(baseline, [candidate])['candidates'][0]
        self.assertFalse(mismatch['matches'])
        self.assertEqual(mismatch['first_differing_record'], 1)
        candidate.write_bytes(b'{"seed":1,"next_rng":42}\n')
        mismatch = tool.compare_seeds(baseline, [candidate])['candidates'][0]
        self.assertFalse(mismatch['matches'])
        self.assertEqual(mismatch['first_differing_record'], 2)


if __name__ == '__main__':
    unittest.main()
