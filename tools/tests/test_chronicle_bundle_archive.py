import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).parents[1]))
spec = importlib.util.spec_from_file_location('bundle_archive', Path(__file__).parents[1] / 'chronicle_bundle_archive.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class BundleArchive(unittest.TestCase):
    def fixture(self, root):
        bundle = root / 'bundle'
        bundle.mkdir()
        (bundle / 'game.exe').write_bytes(b'fixture')
        manifest = {'schema_version': 1, 'files': {
            'game.exe': {'bytes': 7, 'sha256': tool.digest(b'fixture')}}}
        (bundle / 'bundle.json').write_text(json.dumps(manifest))
        return bundle

    def test_archive_is_repeatable_and_readback_matches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = self.fixture(root)
            first = tool.archive(bundle, root / 'first.zip')
            second = tool.archive(bundle, root / 'second.zip')
            self.assertEqual(first['sha256'], second['sha256'])
            with zipfile.ZipFile(root / 'first.zip') as archive:
                self.assertEqual(archive.read('game.exe'), b'fixture')
                self.assertEqual(set(archive.namelist()), {'game.exe', 'bundle.json'})
            self.assertIn(first['sha256'], (root / 'first.zip.sha256').read_text())
            self.assertFalse(first['release_certified'])
            with self.assertRaises(ValueError):
                tool.archive(bundle, root / 'first.zip')

    def test_changed_or_unlisted_data_rejected_before_output(self):
        for case in ('changed', 'save'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                bundle = self.fixture(root)
                (bundle / ('game.exe' if case == 'changed' else 'save.dat')).write_bytes(b'private')
                with self.assertRaises(ValueError):
                    tool.archive(bundle, root / 'bad.zip')
                self.assertFalse((root / 'bad.zip').exists())

    def test_forbidden_output_and_manifest_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = self.fixture(root)
            for output in (bundle / 'bad.zip', root / 'PS2' / 'bad.zip'):
                with self.assertRaises(ValueError):
                    tool.archive(bundle, output)
            for name in ('../escape', '/absolute', 'C:/absolute', 'a\\b', 'ps2/file', 'bundle.json'):
                (bundle / 'bundle.json').write_text(json.dumps({'schema_version': 1, 'files': {name: {}}}))
                with self.subTest(name=name), self.assertRaises(ValueError):
                    tool.archive(bundle, root / 'bad.zip')
            self.assertFalse((root / 'bad.zip').exists())
