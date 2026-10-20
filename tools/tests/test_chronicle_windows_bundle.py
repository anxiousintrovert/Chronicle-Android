import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'windows_bundle', Path(__file__).parents[1] / 'chronicle_windows_bundle.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class WindowsBundle(unittest.TestCase):
    def test_stale_runtime_dll_cannot_use_another_versions_notices(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build, dependencies = root / 'build', root / 'deps'
            build.mkdir()
            llvm = dependencies / 'llvm/llvm-mingw-20260922-ucrt-x86_64/x86_64-w64-mingw32/bin'
            sdl = dependencies / 'sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin'
            llvm.mkdir(parents=True)
            sdl.mkdir(parents=True)
            for name in tool.RUNTIME[2:]:
                (build / name).write_bytes(name.encode())
                ((sdl if name == 'SDL3.dll' else llvm) / name).write_bytes(name.encode())
            tool.verify_runtime_dlls(build, dependencies)
            for name in tool.RUNTIME[2:]:
                (build / name).write_bytes(b'stale runtime')
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'does not match'):
                    tool.verify_runtime_dlls(build, dependencies)
                (build / name).write_bytes(name.encode())

    def test_stages_only_selected_inputs_and_hashes_copied_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'game.exe').write_bytes(b'executable fixture')
            (root / 'save.dat').write_bytes(b'private save')
            output = root / 'bundle'
            manifest = tool.stage({'darkcloud.exe': root / 'game.exe'}, output,
                                  {'checkout_commit': 'fixture'})
            self.assertEqual(set(p.name for p in output.iterdir()),
                             {'darkcloud.exe', 'bundle.json'})
            self.assertEqual(manifest['files']['darkcloud.exe']['sha256'],
                             hashlib.sha256(b'executable fixture').hexdigest())
            self.assertFalse(manifest['release_certified'])
            with self.assertRaises(FileExistsError):
                tool.stage({}, output, {})

    def test_invalid_input_never_creates_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            valid = root / 'valid.exe'
            valid.write_bytes(b'fixture')
            for name in ('missing.exe', '../escape', 'C:/escape', 'ps2/file', 'a\\b'):
                with self.subTest(name=name):
                    with self.assertRaises(ValueError):
                        tool.stage({name: root / 'missing' if name == 'missing.exe' else valid},
                                   root / 'bundle', {})
                    self.assertFalse((root / 'bundle').exists())

    def test_rejects_ps2_destination_and_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(ValueError):
                tool.stage({}, root / 'PS2', {})
            self.assertFalse((root / 'PS2').exists())
            existing = root / 'existing'
            existing.mkdir()
            (existing / 'keep').write_text('original')
            with self.assertRaises(FileExistsError):
                tool.stage({}, existing, {})
            self.assertEqual((existing / 'keep').read_text(), 'original')
